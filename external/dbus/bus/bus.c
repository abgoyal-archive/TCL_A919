
/* -*- mode: C; c-file-style: "gnu" -*- */

#include "bus.h"
#include "activation.h"
#include "connection.h"
#include "services.h"
#include "utils.h"
#include "policy.h"
#include "config-parser.h"
#include "signals.h"
#include "selinux.h"
#include "dir-watch.h"
#include <dbus/dbus-list.h>
#include <dbus/dbus-hash.h>
#include <dbus/dbus-internals.h>

struct BusContext
{
  int refcount;
  char *config_file;
  char *type;
  char *address;
#ifdef WANT_PIDFILE
  char *pidfile;
#endif
  char *user;
  DBusLoop *loop;
  DBusList *servers;
  BusConnections *connections;
  BusActivation *activation;
  BusRegistry *registry;
  BusPolicy *policy;
  BusMatchmaker *matchmaker;
  DBusUserDatabase *user_database;
  BusLimits limits;
  unsigned int fork : 1;
};

static dbus_int32_t server_data_slot = -1;

typedef struct
{
  BusContext *context;
} BusServerData;

#define BUS_SERVER_DATA(server) (dbus_server_get_data ((server), server_data_slot))

static BusContext*
server_get_context (DBusServer *server)
{
  BusContext *context;
  BusServerData *bd;
  
  if (!dbus_server_allocate_data_slot (&server_data_slot))
    return NULL;

  bd = BUS_SERVER_DATA (server);
  if (bd == NULL)
    {
      dbus_server_free_data_slot (&server_data_slot);
      return NULL;
    }

  context = bd->context;

  dbus_server_free_data_slot (&server_data_slot);

  return context;
}

static dbus_bool_t
server_watch_callback (DBusWatch     *watch,
                       unsigned int   condition,
                       void          *data)
{
  /* FIXME this can be done in dbus-mainloop.c
   * if the code in activation.c for the babysitter
   * watch handler is fixed.
   */
  
  return dbus_watch_handle (watch, condition);
}

static dbus_bool_t
add_server_watch (DBusWatch  *watch,
                  void       *data)
{
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);
  
  return _dbus_loop_add_watch (context->loop,
                               watch, server_watch_callback, server,
                               NULL);
}

static void
remove_server_watch (DBusWatch  *watch,
                     void       *data)
{
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);
  
  _dbus_loop_remove_watch (context->loop,
                           watch, server_watch_callback, server);
}


static void
server_timeout_callback (DBusTimeout   *timeout,
                         void          *data)
{
  /* can return FALSE on OOM but we just let it fire again later */
  dbus_timeout_handle (timeout);
}

static dbus_bool_t
add_server_timeout (DBusTimeout *timeout,
                    void        *data)
{
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);

  return _dbus_loop_add_timeout (context->loop,
                                 timeout, server_timeout_callback, server, NULL);
}

static void
remove_server_timeout (DBusTimeout *timeout,
                       void        *data)
{
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);
  
  _dbus_loop_remove_timeout (context->loop,
                             timeout, server_timeout_callback, server);
}

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *data)
{
  BusContext *context = data;
  
  if (!bus_connections_setup_connection (context->connections, new_connection))
    {
      _dbus_verbose ("No memory to setup new connection\n");

      /* if we don't do this, it will get unref'd without
       * being disconnected... kind of strange really
       * that we have to do this, people won't get it right
       * in general.
       */
      dbus_connection_close (new_connection);
    }

  dbus_connection_set_max_received_size (new_connection,
                                         context->limits.max_incoming_bytes);

  dbus_connection_set_max_message_size (new_connection,
                                        context->limits.max_message_size);
  
  /* on OOM, we won't have ref'd the connection so it will die. */
}

static void
free_server_data (void *data)
{
  BusServerData *bd = data;  
  
  dbus_free (bd);
}

static dbus_bool_t
setup_server (BusContext *context,
              DBusServer *server,
              char      **auth_mechanisms,
              DBusError  *error)
{
  BusServerData *bd;

  bd = dbus_new0 (BusServerData, 1);
  if (!dbus_server_set_data (server,
                             server_data_slot,
                             bd, free_server_data))
    {
      dbus_free (bd);
      BUS_SET_OOM (error);
      return FALSE;
    }

  bd->context = context;
  
  if (!dbus_server_set_auth_mechanisms (server, (const char**) auth_mechanisms))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           context, NULL);
  
  if (!dbus_server_set_watch_functions (server,
                                        add_server_watch,
                                        remove_server_watch,
                                        NULL,
                                        server,
                                        NULL))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!dbus_server_set_timeout_functions (server,
                                          add_server_timeout,
                                          remove_server_timeout,
                                          NULL,
                                          server, NULL))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
process_config_first_time_only (BusContext      *context,
				BusConfigParser *parser,
				DBusError       *error)
{
  DBusList *link;
  DBusList **addresses;
  const char *user, *pidfile;
  char **auth_mechanisms;
  DBusList **auth_mechanisms_list;
  int len;
  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  retval = FALSE;
  auth_mechanisms = NULL;

#ifdef WANT_PIDFILE
  /* Check for an existing pid file. Of course this is a race;
   * we'd have to use fcntl() locks on the pid file to
   * avoid that. But we want to check for the pid file
   * before overwriting any existing sockets, etc.
   */
  pidfile = bus_config_parser_get_pidfile (parser);
  if (pidfile != NULL)
    {
      DBusString u;
      DBusStat stbuf;
      
      _dbus_string_init_const (&u, pidfile);
      
      if (_dbus_stat (&u, &stbuf, NULL))
	{
	  dbus_set_error (error, DBUS_ERROR_FAILED,
			  "The pid file \"%s\" exists, if the message bus is not running, remove this file",
			  pidfile);
	  goto failed;
	}
    }
  
  /* keep around the pid filename so we can delete it later */
  context->pidfile = _dbus_strdup (pidfile);
#endif

  /* Build an array of auth mechanisms */
  
  auth_mechanisms_list = bus_config_parser_get_mechanisms (parser);
  len = _dbus_list_get_length (auth_mechanisms_list);

  if (len > 0)
    {
      int i;

      auth_mechanisms = dbus_new0 (char*, len + 1);
      if (auth_mechanisms == NULL)
	{
	  BUS_SET_OOM (error);
	  goto failed;
	}
      
      i = 0;
      link = _dbus_list_get_first_link (auth_mechanisms_list);
      while (link != NULL)
        {
          auth_mechanisms[i] = _dbus_strdup (link->data);
          if (auth_mechanisms[i] == NULL)
	    {
	      BUS_SET_OOM (error);
	      goto failed;
	    }
          link = _dbus_list_get_next_link (auth_mechanisms_list, link);
        }
    }
  else
    {
      auth_mechanisms = NULL;
    }

  /* Listen on our addresses */
  
  addresses = bus_config_parser_get_addresses (parser);  
  
  link = _dbus_list_get_first_link (addresses);
  while (link != NULL)
    {
      DBusServer *server;
      
      server = dbus_server_listen (link->data, error);
      if (server == NULL)
	{
	  _DBUS_ASSERT_ERROR_IS_SET (error);
	  goto failed;
	}
      else if (!setup_server (context, server, auth_mechanisms, error))
	{
	  _DBUS_ASSERT_ERROR_IS_SET (error);
	  goto failed;
	}

      if (!_dbus_list_append (&context->servers, server))
        {
          BUS_SET_OOM (error);
          goto failed;
        }          
      
      link = _dbus_list_get_next_link (addresses, link);
    }

  /* note that type may be NULL */
  context->type = _dbus_strdup (bus_config_parser_get_type (parser));
  if (bus_config_parser_get_type (parser) != NULL && context->type == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  user = bus_config_parser_get_user (parser);
  if (user != NULL)
    {
      context->user = _dbus_strdup (user);
      if (context->user == NULL)
	{
	  BUS_SET_OOM (error);
	  goto failed;
	}
    }

  context->fork = bus_config_parser_get_fork (parser);
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  retval = TRUE;

 failed:
  dbus_free_string_array (auth_mechanisms);
  return retval;
}

static dbus_bool_t
process_config_every_time (BusContext      *context,
			   BusConfigParser *parser,
			   dbus_bool_t      is_reload,
			   DBusError       *error)
{
  DBusString full_address;
  DBusList *link;
  char *addr;

  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  addr = NULL;
  retval = FALSE;

  if (!_dbus_string_init (&full_address))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  /* get our limits and timeout lengths */
  bus_config_parser_get_limits (parser, &context->limits);

  context->policy = bus_config_parser_steal_policy (parser);
  _dbus_assert (context->policy != NULL);

  /* We have to build the address backward, so that
   * <listen> later in the config file have priority
   */
  link = _dbus_list_get_last_link (&context->servers);
  while (link != NULL)
    {
      addr = dbus_server_get_address (link->data);
      if (addr == NULL)
        {
          BUS_SET_OOM (error);
          goto failed;
        }

      if (_dbus_string_get_length (&full_address) > 0)
        {
          if (!_dbus_string_append (&full_address, ";"))
            {
              BUS_SET_OOM (error);
              goto failed;
            }
        }

      if (!_dbus_string_append (&full_address, addr))
        {
          BUS_SET_OOM (error);
          goto failed;
        }

      dbus_free (addr);
      addr = NULL;

      link = _dbus_list_get_prev_link (&context->servers, link);
    }

  if (is_reload)
    dbus_free (context->address);

  if (!_dbus_string_copy_data (&full_address, &context->address))
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  /* Create activation subsystem */
  
  if (is_reload)
    bus_activation_unref (context->activation);
  
  context->activation = bus_activation_new (context, &full_address,
                                            bus_config_parser_get_service_dirs (parser),
                                            error);
  if (context->activation == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }

  /* Drop existing conf-dir watches (if applicable) */

  if (is_reload)
    bus_drop_all_directory_watches ();

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  retval = TRUE;

 failed:
  _dbus_string_free (&full_address);
  
  if (addr)
    dbus_free (addr);

  return retval;
}

static dbus_bool_t
process_config_postinit (BusContext      *context,
			 BusConfigParser *parser,
			 DBusError       *error)
{
  DBusHashTable *service_context_table;

  service_context_table = bus_config_parser_steal_service_context_table (parser);
  if (!bus_registry_set_service_context_table (context->registry,
					       service_context_table))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  _dbus_hash_table_unref (service_context_table);

  /* Watch all conf directories */
  _dbus_list_foreach (bus_config_parser_get_conf_dirs (parser),
		      (DBusForeachFunction) bus_watch_directory,
		      context);

  return TRUE;
}

BusContext*
bus_context_new (const DBusString *config_file,
                 ForceForkSetting  force_fork,
                 int               print_addr_fd,
                 int               print_pid_fd,
                 DBusError        *error)
{
  BusContext *context;
  BusConfigParser *parser;
  DBusCredentials creds;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  context = NULL;
  parser = NULL;

  if (!dbus_server_allocate_data_slot (&server_data_slot))
    {
      BUS_SET_OOM (error);
      return NULL;
    }

  context = dbus_new0 (BusContext, 1);
  if (context == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  context->refcount = 1;

  if (!_dbus_string_copy_data (config_file, &context->config_file))
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  context->loop = _dbus_loop_new ();
  if (context->loop == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  context->registry = bus_registry_new (context);
  if (context->registry == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  parser = bus_config_load (config_file, TRUE, NULL, error);
  if (parser == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }
  
  if (!process_config_first_time_only (context, parser, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }
  if (!process_config_every_time (context, parser, FALSE, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }
  
  /* we need another ref of the server data slot for the context
   * to own
   */
  if (!dbus_server_allocate_data_slot (&server_data_slot))
    _dbus_assert_not_reached ("second ref of server data slot failed");

  context->user_database = _dbus_user_database_new ();
  if (context->user_database == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  /* Note that we don't know whether the print_addr_fd is
   * one of the sockets we're using to listen on, or some
   * other random thing. But I think the answer is "don't do
   * that then"
   */
  if (print_addr_fd >= 0)
    {
      DBusString addr;
      const char *a = bus_context_get_address (context);
      int bytes;
      
      _dbus_assert (a != NULL);
      if (!_dbus_string_init (&addr))
        {
          BUS_SET_OOM (error);
          goto failed;
        }
      
      if (!_dbus_string_append (&addr, a) ||
          !_dbus_string_append (&addr, "\n"))
        {
          _dbus_string_free (&addr);
          BUS_SET_OOM (error);
          goto failed;
        }

      bytes = _dbus_string_get_length (&addr);
      if (_dbus_write_socket (print_addr_fd, &addr, 0, bytes) != bytes)
        {
          dbus_set_error (error, DBUS_ERROR_FAILED,
                          "Printing message bus address: %s\n",
                          _dbus_strerror (errno));
          _dbus_string_free (&addr);
          goto failed;
        }

      if (print_addr_fd > 2)
        _dbus_close_socket (print_addr_fd, NULL);

      _dbus_string_free (&addr);
    }
  
  context->connections = bus_connections_new (context);
  if (context->connections == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  context->matchmaker = bus_matchmaker_new ();
  if (context->matchmaker == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  /* check user before we fork */
  if (context->user != NULL)
    {
      DBusString u;

      _dbus_string_init_const (&u, context->user);

      if (!_dbus_credentials_from_username (&u, &creds) ||
          creds.uid < 0 ||
          creds.gid < 0)
        {
          dbus_set_error (error, DBUS_ERROR_FAILED,
                          "Could not get UID and GID for username \"%s\"",
                          context->user);
          goto failed;
        }
    }

  /* Now become a daemon if appropriate */
  if ((force_fork != FORK_NEVER && context->fork) || force_fork == FORK_ALWAYS)
    {
      DBusString u;
#ifdef WANT_PIDFILE
      if (context->pidfile)
        _dbus_string_init_const (&u, context->pidfile);
      
      if (!_dbus_become_daemon (context->pidfile ? &u : NULL, 
				print_pid_fd,
				error))
# else
      if (!_dbus_become_daemon (NULL, 
				0,
				error))
#endif
	{
	  _DBUS_ASSERT_ERROR_IS_SET (error);
	  goto failed;
	}
    }
#ifdef WANT_PIDFILE
  else
    {
      /* Need to write PID file for ourselves, not for the child process */
      if (context->pidfile != NULL)
        {
          DBusString u;

          _dbus_string_init_const (&u, context->pidfile);
          
          if (!_dbus_write_pid_file (&u, _dbus_getpid (), error))
	    {
	      _DBUS_ASSERT_ERROR_IS_SET (error);
	      goto failed;
	    }
        }
    }
#endif

  /* Write PID if requested */
  if (print_pid_fd >= 0)
    {
      DBusString pid;
      int bytes;

      if (!_dbus_string_init (&pid))
        {
          BUS_SET_OOM (error);
          goto failed;
        }
      
      if (!_dbus_string_append_int (&pid, _dbus_getpid ()) ||
          !_dbus_string_append (&pid, "\n"))
        {
          _dbus_string_free (&pid);
          BUS_SET_OOM (error);
          goto failed;
        }

      bytes = _dbus_string_get_length (&pid);
      if (_dbus_write_socket (print_pid_fd, &pid, 0, bytes) != bytes)
        {
          dbus_set_error (error, DBUS_ERROR_FAILED,
                          "Printing message bus PID: %s\n",
                          _dbus_strerror (errno));
          _dbus_string_free (&pid);
          goto failed;
        }

      if (print_pid_fd > 2)
        _dbus_close_socket (print_pid_fd, NULL);
      
      _dbus_string_free (&pid);
    }

  if (!bus_selinux_full_init ())
    {
      _dbus_warn ("SELinux initialization failed\n");
    }

  if (!process_config_postinit (context, parser, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }

  if (parser != NULL)
    {
      bus_config_parser_unref (parser);
      parser = NULL;
    }
  
  /* Here we change our credentials if required,
   * as soon as we've set up our sockets and pidfile
   */
  if (context->user != NULL)
    {
      if (!_dbus_change_identity (creds.uid, creds.gid, error))
	{
	  _DBUS_ASSERT_ERROR_IS_SET (error);
	  goto failed;
	}
    }
  
  dbus_server_free_data_slot (&server_data_slot);
  
  return context;
  
 failed:  
  if (parser != NULL)
    bus_config_parser_unref (parser);
  if (context != NULL)
    bus_context_unref (context);

  if (server_data_slot >= 0)
    dbus_server_free_data_slot (&server_data_slot);
  
  return NULL;
}

dbus_bool_t
bus_context_reload_config (BusContext *context,
			   DBusError  *error)
{
  BusConfigParser *parser;
  DBusString config_file;
  dbus_bool_t ret;

  /* Flush the user database cache */
  _dbus_user_database_flush(context->user_database);

  ret = FALSE;
  _dbus_string_init_const (&config_file, context->config_file);
  parser = bus_config_load (&config_file, TRUE, NULL, error);
  if (parser == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }
  
  if (!process_config_every_time (context, parser, TRUE, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }
  if (!process_config_postinit (context, parser, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }
  ret = TRUE;

 failed:  
  if (parser != NULL)
    bus_config_parser_unref (parser);
  return ret;
}

static void
shutdown_server (BusContext *context,
                 DBusServer *server)
{
  if (server == NULL ||
      !dbus_server_get_is_connected (server))
    return;
  
  if (!dbus_server_set_watch_functions (server,
                                        NULL, NULL, NULL,
                                        context,
                                        NULL))
    _dbus_assert_not_reached ("setting watch functions to NULL failed");
  
  if (!dbus_server_set_timeout_functions (server,
                                          NULL, NULL, NULL,
                                          context,
                                          NULL))
    _dbus_assert_not_reached ("setting timeout functions to NULL failed");
  
  dbus_server_disconnect (server);
}

void
bus_context_shutdown (BusContext  *context)
{
  DBusList *link;

  link = _dbus_list_get_first_link (&context->servers);
  while (link != NULL)
    {
      shutdown_server (context, link->data);

      link = _dbus_list_get_next_link (&context->servers, link);
    }
}

BusContext *
bus_context_ref (BusContext *context)
{
  _dbus_assert (context->refcount > 0);
  context->refcount += 1;

  return context;
}

void
bus_context_unref (BusContext *context)
{
  _dbus_assert (context->refcount > 0);
  context->refcount -= 1;

  if (context->refcount == 0)
    {
      DBusList *link;
      
      _dbus_verbose ("Finalizing bus context %p\n", context);
      
      bus_context_shutdown (context);

      if (context->connections)
        {
          bus_connections_unref (context->connections);
          context->connections = NULL;
        }
      
      if (context->registry)
        {
          bus_registry_unref (context->registry);
          context->registry = NULL;
        }
      
      if (context->activation)
        {
          bus_activation_unref (context->activation);
          context->activation = NULL;
        }

      link = _dbus_list_get_first_link (&context->servers);
      while (link != NULL)
        {
          dbus_server_unref (link->data);
          
          link = _dbus_list_get_next_link (&context->servers, link);
        }
      _dbus_list_clear (&context->servers);

      if (context->policy)
        {
          bus_policy_unref (context->policy);
          context->policy = NULL;
        }
      
      if (context->loop)
        {
          _dbus_loop_unref (context->loop);
          context->loop = NULL;
        }

      if (context->matchmaker)
        {
          bus_matchmaker_unref (context->matchmaker);
          context->matchmaker = NULL;
        }
      
      dbus_free (context->config_file);
      dbus_free (context->type);
      dbus_free (context->address);
      dbus_free (context->user);

#ifdef WANT_PIDFILE
      if (context->pidfile)
	{
          DBusString u;
          _dbus_string_init_const (&u, context->pidfile);

          /* Deliberately ignore errors here, since there's not much
	   * we can do about it, and we're exiting anyways.
	   */
	  _dbus_delete_file (&u, NULL);

          dbus_free (context->pidfile); 
	}
#endif

      if (context->user_database != NULL)
	_dbus_user_database_unref (context->user_database);
      
      dbus_free (context);

      dbus_server_free_data_slot (&server_data_slot);
    }
}

/* type may be NULL */
const char*
bus_context_get_type (BusContext *context)
{
  return context->type;
}

const char*
bus_context_get_address (BusContext *context)
{
  return context->address;
}

BusRegistry*
bus_context_get_registry (BusContext  *context)
{
  return context->registry;
}

BusConnections*
bus_context_get_connections (BusContext  *context)
{
  return context->connections;
}

BusActivation*
bus_context_get_activation (BusContext  *context)
{
  return context->activation;
}

BusMatchmaker*
bus_context_get_matchmaker (BusContext  *context)
{
  return context->matchmaker;
}

DBusLoop*
bus_context_get_loop (BusContext *context)
{
  return context->loop;
}

DBusUserDatabase*
bus_context_get_user_database (BusContext *context)
{
  return context->user_database;
}

dbus_bool_t
bus_context_allow_user (BusContext   *context,
                        unsigned long uid)
{
  return bus_policy_allow_user (context->policy,
                                context->user_database,
                                uid);
}

BusPolicy *
bus_context_get_policy (BusContext *context)
{
  return context->policy;
}

BusClientPolicy*
bus_context_create_client_policy (BusContext      *context,
                                  DBusConnection  *connection,
                                  DBusError       *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  return bus_policy_create_client_policy (context->policy, connection,
                                          error);
}

int
bus_context_get_activation_timeout (BusContext *context)
{
  
  return context->limits.activation_timeout;
}

int
bus_context_get_auth_timeout (BusContext *context)
{
  return context->limits.auth_timeout;
}

int
bus_context_get_max_completed_connections (BusContext *context)
{
  return context->limits.max_completed_connections;
}

int
bus_context_get_max_incomplete_connections (BusContext *context)
{
  return context->limits.max_incomplete_connections;
}

int
bus_context_get_max_connections_per_user (BusContext *context)
{
  return context->limits.max_connections_per_user;
}

int
bus_context_get_max_pending_activations (BusContext *context)
{
  return context->limits.max_pending_activations;
}

int
bus_context_get_max_services_per_connection (BusContext *context)
{
  return context->limits.max_services_per_connection;
}

int
bus_context_get_max_match_rules_per_connection (BusContext *context)
{
  return context->limits.max_match_rules_per_connection;
}

int
bus_context_get_max_replies_per_connection (BusContext *context)
{
  return context->limits.max_replies_per_connection;
}

int
bus_context_get_reply_timeout (BusContext *context)
{
  return context->limits.reply_timeout;
}

dbus_bool_t
bus_context_check_security_policy (BusContext     *context,
                                   BusTransaction *transaction,
                                   DBusConnection *sender,
                                   DBusConnection *addressed_recipient,
                                   DBusConnection *proposed_recipient,
                                   DBusMessage    *message,
                                   DBusError      *error)
{
  BusClientPolicy *sender_policy;
  BusClientPolicy *recipient_policy;
  int type;
  dbus_bool_t requested_reply;
  
  type = dbus_message_get_type (message);
  
  /* dispatch.c was supposed to ensure these invariants */
  _dbus_assert (dbus_message_get_destination (message) != NULL ||
                type == DBUS_MESSAGE_TYPE_SIGNAL ||
                (sender == NULL && !bus_connection_is_active (proposed_recipient)));
  _dbus_assert (type == DBUS_MESSAGE_TYPE_SIGNAL ||
                addressed_recipient != NULL ||
                strcmp (dbus_message_get_destination (message), DBUS_SERVICE_DBUS) == 0);
  
  switch (type)
    {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
    case DBUS_MESSAGE_TYPE_SIGNAL:
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
    case DBUS_MESSAGE_TYPE_ERROR:
      break;
      
    default:
      _dbus_verbose ("security check disallowing message of unknown type %d\n",
                     type);

      dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                      "Message bus will not accept messages of unknown type\n");
              
      return FALSE;
    }

  requested_reply = FALSE;
  
  if (sender != NULL)
    {
      const char *dest;

      dest = dbus_message_get_destination (message);
	
      /* First verify the SELinux access controls.  If allowed then
       * go on with the standard checks.
       */
      if (!bus_selinux_allows_send (sender, proposed_recipient,
				    dbus_message_type_to_string (dbus_message_get_type (message)),
				    dbus_message_get_interface (message),
				    dbus_message_get_member (message),
				    dbus_message_get_error_name (message),
				    dest ? dest : DBUS_SERVICE_DBUS, error))
        {

	  if (dbus_error_is_set (error) &&
	      dbus_error_has_name (error, DBUS_ERROR_NO_MEMORY))
	    {
	      return FALSE;
	    }
	  

          dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                          "An SELinux policy prevents this sender "
                          "from sending this message to this recipient "
                          "(rejected message had interface \"%s\" "
                          "member \"%s\" error name \"%s\" destination \"%s\")",
                          dbus_message_get_interface (message) ?
                          dbus_message_get_interface (message) : "(unset)",
                          dbus_message_get_member (message) ?
                          dbus_message_get_member (message) : "(unset)",
                          dbus_message_get_error_name (message) ?
                          dbus_message_get_error_name (message) : "(unset)",
                          dest ? dest : DBUS_SERVICE_DBUS);
          _dbus_verbose ("SELinux security check denying send to service\n");
          return FALSE;
        }
       
      if (bus_connection_is_active (sender))
        {
          sender_policy = bus_connection_get_policy (sender);
          _dbus_assert (sender_policy != NULL);
          
          /* Fill in requested_reply variable with TRUE if this is a
           * reply and the reply was pending.
           */
          if (dbus_message_get_reply_serial (message) != 0)
            {
              if (proposed_recipient != NULL /* not to the bus driver */ &&
                  addressed_recipient == proposed_recipient /* not eavesdropping */)
                {
                  DBusError error2;                  
                  
                  dbus_error_init (&error2);
                  requested_reply = bus_connections_check_reply (bus_connection_get_connections (sender),
                                                                 transaction,
                                                                 sender, addressed_recipient, message,
                                                                 &error2);
                  if (dbus_error_is_set (&error2))
                    {
                      dbus_move_error (&error2, error);
                      return FALSE;
                    }
                }
            }
        }
      else
        {
          /* Policy for inactive connections is that they can only send
           * the hello message to the bus driver
           */
          if (proposed_recipient == NULL &&
              dbus_message_is_method_call (message,
                                           DBUS_INTERFACE_DBUS,
                                           "Hello"))
            {
              _dbus_verbose ("security check allowing %s message\n",
                             "Hello");
              return TRUE;
            }
          else
            {
              _dbus_verbose ("security check disallowing non-%s message\n",
                             "Hello");

              dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                              "Client tried to send a message other than %s without being registered",
                              "Hello");
              
              return FALSE;
            }
        }
    }
  else
    {
      sender_policy = NULL;

      /* If the sender is the bus driver, we assume any reply was a
       * requested reply as bus driver won't send bogus ones
       */
      if (addressed_recipient == proposed_recipient /* not eavesdropping */ &&
          dbus_message_get_reply_serial (message) != 0)
        requested_reply = TRUE;
    }

  _dbus_assert ((sender != NULL && sender_policy != NULL) ||
                (sender == NULL && sender_policy == NULL));
  
  if (proposed_recipient != NULL)
    {
      /* only the bus driver can send to an inactive recipient (as it
       * owns no services, so other apps can't address it). Inactive
       * recipients can receive any message.
       */
      if (bus_connection_is_active (proposed_recipient))
        {
          recipient_policy = bus_connection_get_policy (proposed_recipient);
          _dbus_assert (recipient_policy != NULL);
        }
      else if (sender == NULL)
        {
          _dbus_verbose ("security check using NULL recipient policy for message from bus\n");
          recipient_policy = NULL;
        }
      else
        {
          _dbus_assert_not_reached ("a message was somehow sent to an inactive recipient from a source other than the message bus\n");
          recipient_policy = NULL;
        }
    }
  else
    recipient_policy = NULL;
  
  _dbus_assert ((proposed_recipient != NULL && recipient_policy != NULL) ||
                (proposed_recipient != NULL && sender == NULL && recipient_policy == NULL) ||
                (proposed_recipient == NULL && recipient_policy == NULL));
  
  if (sender_policy &&
      !bus_client_policy_check_can_send (sender_policy,
                                         context->registry,
                                         requested_reply,
                                         proposed_recipient,
                                         message))
    {
      const char *dest;

      dest = dbus_message_get_destination (message);
      dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                      "A security policy in place prevents this sender "
                      "from sending this message to this recipient, "
                      "see message bus configuration file (rejected message "
                      "had interface \"%s\" member \"%s\" error name \"%s\" destination \"%s\")",
                      dbus_message_get_interface (message) ?
                      dbus_message_get_interface (message) : "(unset)",
                      dbus_message_get_member (message) ?
                      dbus_message_get_member (message) : "(unset)",
                      dbus_message_get_error_name (message) ?
                      dbus_message_get_error_name (message) : "(unset)",
                      dest ? dest : DBUS_SERVICE_DBUS);
      _dbus_verbose ("security policy disallowing message due to sender policy\n");
      return FALSE;
    }

  if (recipient_policy &&
      !bus_client_policy_check_can_receive (recipient_policy,
                                            context->registry,
                                            requested_reply,
                                            sender,
                                            addressed_recipient, proposed_recipient,
                                            message))
    {
      const char *dest;

      dest = dbus_message_get_destination (message);
      dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                      "A security policy in place prevents this recipient "
                      "from receiving this message from this sender, "
                      "see message bus configuration file (rejected message "
                      "had interface \"%s\" member \"%s\" error name \"%s\" destination \"%s\" reply serial %u requested_reply=%d)",
                      dbus_message_get_interface (message) ?
                      dbus_message_get_interface (message) : "(unset)",
                      dbus_message_get_member (message) ?
                      dbus_message_get_member (message) : "(unset)",
                      dbus_message_get_error_name (message) ?
                      dbus_message_get_error_name (message) : "(unset)",
                      dest ? dest : DBUS_SERVICE_DBUS,
                      dbus_message_get_reply_serial (message),
                      requested_reply);
      _dbus_verbose ("security policy disallowing message due to recipient policy\n");
      return FALSE;
    }

  /* See if limits on size have been exceeded */
  if (proposed_recipient &&
      dbus_connection_get_outgoing_size (proposed_recipient) >
      context->limits.max_outgoing_bytes)
    {
      const char *dest;

      dest = dbus_message_get_destination (message);
      dbus_set_error (error, DBUS_ERROR_LIMITS_EXCEEDED,
                      "The destination service \"%s\" has a full message queue",
                      dest ? dest : (proposed_recipient ?
                                     bus_connection_get_name (proposed_recipient) : 
                                     DBUS_SERVICE_DBUS));
      _dbus_verbose ("security policy disallowing message due to full message queue\n");
      return FALSE;
    }

  /* Record that we will allow a reply here in the future (don't
   * bother if the recipient is the bus or this is an eavesdropping
   * connection). Only the addressed recipient may reply.
   */
  if (type == DBUS_MESSAGE_TYPE_METHOD_CALL &&
      sender && 
      addressed_recipient &&
      addressed_recipient == proposed_recipient && /* not eavesdropping */
      !bus_connections_expect_reply (bus_connection_get_connections (sender),
                                     transaction,
                                     sender, addressed_recipient,
                                     message, error))
    {
      _dbus_verbose ("Failed to record reply expectation or problem with the message expecting a reply\n");
      return FALSE;
    }
  
  _dbus_verbose ("security policy allowing message\n");
  return TRUE;
}
