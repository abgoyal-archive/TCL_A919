#include "tomcrypt.h"


#ifdef LTC_HMAC

#define HMAC_BLOCKSIZE hash_descriptor[hash].blocksize

int hmac_init(hmac_state *hmac, int hash, const unsigned char *key, unsigned long keylen)
{
    unsigned char *buf;
    unsigned long hashsize;
    unsigned long i, z;
    int err;

    LTC_ARGCHK(hmac != NULL);
    LTC_ARGCHK(key  != NULL);

    /* valid hash? */
    if ((err = hash_is_valid(hash)) != CRYPT_OK) {
        return err;
    }
    hmac->hash = hash;
    hashsize   = hash_descriptor[hash].hashsize;

    /* valid key length? */
    if (keylen == 0) {
        return CRYPT_INVALID_KEYSIZE;
    }

    /* allocate ram for buf */
    buf = XMALLOC(HMAC_BLOCKSIZE);
    if (buf == NULL) {
       return CRYPT_MEM;
    }

    /* allocate memory for key */
    hmac->key = XMALLOC(HMAC_BLOCKSIZE);
    if (hmac->key == NULL) {
       XFREE(buf);
       return CRYPT_MEM;
    }

    /* (1) make sure we have a large enough key */
    if(keylen > HMAC_BLOCKSIZE) {
        z = HMAC_BLOCKSIZE;
        if ((err = hash_memory(hash, key, keylen, hmac->key, &z)) != CRYPT_OK) {
           goto LBL_ERR;
        }
        if(hashsize < HMAC_BLOCKSIZE) {
            zeromem((hmac->key) + hashsize, (size_t)(HMAC_BLOCKSIZE - hashsize));
        }
        keylen = hashsize;
    } else {
        XMEMCPY(hmac->key, key, (size_t)keylen);
        if(keylen < HMAC_BLOCKSIZE) {
            zeromem((hmac->key) + keylen, (size_t)(HMAC_BLOCKSIZE - keylen));
        }
    }

    /* Create the initial vector for step (3) */
    for(i=0; i < HMAC_BLOCKSIZE;   i++) {
       buf[i] = hmac->key[i] ^ 0x36;
    }

    /* Pre-pend that to the hash data */
    if ((err = hash_descriptor[hash].init(&hmac->md)) != CRYPT_OK) {
       goto LBL_ERR;
    }

    if ((err = hash_descriptor[hash].process(&hmac->md, buf, HMAC_BLOCKSIZE)) != CRYPT_OK) {
       goto LBL_ERR;
    }
    goto done;
LBL_ERR:
    /* free the key since we failed */
    XFREE(hmac->key);
done:
#ifdef LTC_CLEAN_STACK
   zeromem(buf, HMAC_BLOCKSIZE);
#endif
 
   XFREE(buf);
   return err;    
}

#endif

/* $Source: /cvs/libtom/libtomcrypt/src/mac/hmac/hmac_init.c,v $ */
/* $Revision: 1.5 $ */
/* $Date: 2006/11/03 00:39:49 $ */