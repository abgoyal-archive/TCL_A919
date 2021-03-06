#include "tomcrypt.h"


#ifdef LTC_DER
int der_length_octet_string(unsigned long noctets, unsigned long *outlen)
{
   LTC_ARGCHK(outlen != NULL);

   if (noctets < 128) {
      /* 04 LL DD DD DD ... */
      *outlen = 2 + noctets;
   } else if (noctets < 256) {
      /* 04 81 LL DD DD DD ... */
      *outlen = 3 + noctets;
   } else if (noctets < 65536UL) {
      /* 04 82 LL LL DD DD DD ... */
      *outlen = 4 + noctets;
   } else if (noctets < 16777216UL) {
      /* 04 83 LL LL LL DD DD DD ... */
      *outlen = 5 + noctets;
   } else {
      return CRYPT_INVALID_ARG;
   }

   return CRYPT_OK;
}

#endif


/* $Source: /cvs/libtom/libtomcrypt/src/pk/asn1/der/octet/der_length_octet_string.c,v $ */
/* $Revision: 1.2 $ */
/* $Date: 2006/03/31 14:15:35 $ */
