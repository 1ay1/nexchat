/*
 * ssl.c v0.0.3
 * Copyright (C) 2000  --  DaP <profeta@freemail.c3.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifdef __APPLE__
#define __AVAILABILITYMACROS__
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#endif

#include "inet.h"				  /* make it first to avoid macro redefinitions */
#include <openssl/ssl.h>		  /* SSL_() */
#include <openssl/err.h>		  /* ERR_() */
#include <openssl/x509v3.h>
#ifdef WIN32
#include <openssl/rand.h>		  /* RAND_seed() */
#endif
#include "config.h"
#include <time.h>				  /* asctime() */
#include <string.h>				  /* strncpy() */
#include "ssl.h"				  /* struct cert_info */

#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include "util.h"

/* globals */
static struct chiper_info chiper_info;		/* static buffer for _SSL_get_cipher_info() */
static char err_buf[256];			/* generic error buffer */


/* +++++ Internal functions +++++ */

static void
__SSL_fill_err_buf (char *funcname)
{
	int err;
	char buf[256];


	err = ERR_get_error ();
	ERR_error_string (err, buf);
	g_snprintf (err_buf, sizeof (err_buf), "%s: %s (%d)\n", funcname, buf, err);
}


static void
__SSL_critical_error (char *funcname)
{
	__SSL_fill_err_buf (funcname);
	fprintf (stderr, "%s\n", err_buf);

	exit (1);
}

/* +++++ SSL functions +++++ */

SSL_CTX *
_SSL_context_init (void (*info_cb_func))
{
	SSL_CTX *ctx;

	ctx = SSL_CTX_new (TLS_client_method ());

	SSL_CTX_set_session_cache_mode (ctx, SSL_SESS_CACHE_BOTH);
	SSL_CTX_set_timeout (ctx, 300);
	SSL_CTX_set_options (ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3
							  |SSL_OP_NO_COMPRESSION
							  |SSL_OP_SINGLE_DH_USE|SSL_OP_SINGLE_ECDH_USE
							  |SSL_OP_NO_TICKET
							  |SSL_OP_CIPHER_SERVER_PREFERENCE);
	/* TLS 1.0 and 1.1 are deprecated (RFC 8996) */
	SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION);

	/* used in SSL_connect(), SSL_accept() */
	SSL_CTX_set_info_callback (ctx, info_cb_func);

	return(ctx);
}

static void
ASN1_TIME_snprintf (char *buf, int buf_len, ASN1_TIME * tm)
{
	char *expires = NULL;
	BIO *inMem = BIO_new (BIO_s_mem ());

	ASN1_TIME_print (inMem, tm);
	BIO_get_mem_data (inMem, &expires);
	buf[0] = 0;
	if (expires != NULL)
	{
		/* expires is not \0 terminated */
		safe_strcpy (buf, expires, MIN(24, buf_len));
	}
	BIO_free (inMem);
}


static void
broke_oneline (char *oneline, char *parray[])
{
	char *pt, *ppt;
	int i;


	i = 0;
	ppt = pt = oneline + 1;
	while ((pt = strchr (pt, '/')))
	{
		*pt = 0;
		parray[i++] = ppt;
		ppt = ++pt;
	}
	parray[i++] = ppt;
	parray[i] = NULL;
}


/*
    FIXME: Master-Key, Extensions, CA bits
	    (openssl x509 -text -in servcert.pem)
*/
int
_SSL_get_cert_info (struct cert_info *cert_info, SSL * ssl)
{
	X509 *peer_cert;
	const X509_PUBKEY *key;
	X509_ALGOR *algor = NULL;
	EVP_PKEY *peer_pkey;
	char notBefore[64];
	char notAfter[64];
	int alg;
	int sign_alg;


	if (!(peer_cert = SSL_get1_peer_certificate (ssl)))
		return (1);				  /* FATAL? */

	X509_NAME_oneline (X509_get_subject_name (peer_cert), cert_info->subject,
							 sizeof (cert_info->subject));
	X509_NAME_oneline (X509_get_issuer_name (peer_cert), cert_info->issuer,
							 sizeof (cert_info->issuer));
	broke_oneline (cert_info->subject, cert_info->subject_word);
	broke_oneline (cert_info->issuer, cert_info->issuer_word);

	key = X509_get_X509_PUBKEY(peer_cert);
	if (!X509_PUBKEY_get0_param(NULL, NULL, 0, &algor, key))
		return 1;

	alg = OBJ_obj2nid (algor->algorithm);
	sign_alg = X509_get_signature_nid (peer_cert);
	ASN1_TIME_snprintf (notBefore, sizeof (notBefore),
							  X509_get_notBefore (peer_cert));
	ASN1_TIME_snprintf (notAfter, sizeof (notAfter),
							  X509_get_notAfter (peer_cert));

	peer_pkey = X509_get_pubkey (peer_cert);

	safe_strcpy (cert_info->algorithm,
				(alg == NID_undef) ? "Unknown" : OBJ_nid2ln (alg),
				sizeof (cert_info->algorithm));
	cert_info->algorithm_bits = EVP_PKEY_bits (peer_pkey);
	safe_strcpy (cert_info->sign_algorithm,
				(sign_alg == NID_undef) ? "Unknown" : OBJ_nid2ln (sign_alg),
				sizeof (cert_info->sign_algorithm));
	/* EVP_PKEY_bits(ca_pkey)); */
	cert_info->sign_algorithm_bits = 0;
	safe_strcpy (cert_info->notbefore, notBefore, sizeof (cert_info->notbefore));
	safe_strcpy (cert_info->notafter, notAfter, sizeof (cert_info->notafter));

	EVP_PKEY_free (peer_pkey);

	/* SSL_SESSION_print_fp(stdout, SSL_get_session(ssl)); */
/*
	if (ssl->session->sess_cert->peer_rsa_tmp) {
		tmp_pkey = EVP_PKEY_new();
		EVP_PKEY_assign_RSA(tmp_pkey, ssl->session->sess_cert->peer_rsa_tmp);
		cert_info->rsa_tmp_bits = EVP_PKEY_bits (tmp_pkey);
		EVP_PKEY_free(tmp_pkey);
	} else
		fprintf(stderr, "REMOTE SIDE DOESN'T PROVIDES ->peer_rsa_tmp\n");
*/
	cert_info->rsa_tmp_bits = 0;

	X509_free (peer_cert);

	return (0);
}


struct chiper_info *
_SSL_get_cipher_info (SSL * ssl)
{
	const SSL_CIPHER *c;


	c = SSL_get_current_cipher (ssl);
	safe_strcpy (chiper_info.version, SSL_CIPHER_get_version (c),
				sizeof (chiper_info.version));
	safe_strcpy (chiper_info.chiper, SSL_CIPHER_get_name (c),
				sizeof (chiper_info.chiper));
	SSL_CIPHER_get_bits (c, &chiper_info.chiper_bits);

	return (&chiper_info);
}


int
_SSL_send (SSL * ssl, char *buf, int len)
{
	int num;


	num = SSL_write (ssl, buf, len);

	switch (SSL_get_error (ssl, num))
	{
	case SSL_ERROR_SSL:			  /* setup errno! */
		/* ??? */
		__SSL_fill_err_buf ("SSL_write");
		fprintf (stderr, "%s\n", err_buf);
		break;
	case SSL_ERROR_SYSCALL:
		/* ??? */
		perror ("SSL_write/write");
		break;
	case SSL_ERROR_ZERO_RETURN:
		/* fprintf(stderr, "SSL closed on write\n"); */
		break;
	}

	return (num);
}


int
_SSL_recv (SSL * ssl, char *buf, int len)
{
	int num;


	num = SSL_read (ssl, buf, len);

	switch (SSL_get_error (ssl, num))
	{
	case SSL_ERROR_SSL:
		/* ??? */
		__SSL_fill_err_buf ("SSL_read");
		fprintf (stderr, "%s\n", err_buf);
		break;
	case SSL_ERROR_SYSCALL:
		/* ??? */
		if (!would_block ())
			perror ("SSL_read/read");
		break;
	case SSL_ERROR_ZERO_RETURN:
		/* fprintf(stdeerr, "SSL closed on read\n"); */
		break;
	}

	return (num);
}


SSL *
_SSL_socket (SSL_CTX *ctx, int sd)
{
	SSL *ssl;
	const SSL_METHOD *method;

	if (!(ssl = SSL_new (ctx)))
		/* FATAL */
		__SSL_critical_error ("SSL_new");

	SSL_set_fd (ssl, sd);

	method = SSL_CTX_get_ssl_method (ctx);
	if (method == TLS_client_method())
		SSL_set_connect_state (ssl);
	else
	        SSL_set_accept_state(ssl);

	return (ssl);
}


char *
_SSL_set_verify (SSL_CTX *ctx, void *verify_callback)
{
#ifdef DEFAULT_CERT_FILE
	if (!SSL_CTX_load_verify_locations (ctx, DEFAULT_CERT_FILE, NULL))
	{
		__SSL_fill_err_buf ("SSL_CTX_load_verify_locations");
		return (err_buf);
	}
#else
	if (!SSL_CTX_set_default_verify_paths (ctx))
	{
		__SSL_fill_err_buf ("SSL_CTX_set_default_verify_paths");
		return (err_buf);
	}
#endif

	SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, verify_callback);

	return (NULL);
}


void
_SSL_close (SSL * ssl)
{
	SSL_set_shutdown (ssl, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
	SSL_free (ssl);
}

/* the certificate names no DNS host in subjectAltName: then (RFC 6125
   6.4.4) its subject CN is the name. OpenSSL 3 did that by itself;
   OpenSSL 4 only when asked, and older servers' self-signed certs (and
   tests/sts.py's) have just a CN */
static gboolean
cert_has_dns_san (X509 *cert)
{
	GENERAL_NAMES *names = X509_get_ext_d2i (cert, NID_subject_alt_name, NULL, NULL);
	gboolean dns = FALSE;
	int i;

	for (i = 0; names && i < sk_GENERAL_NAME_num (names); i++)
		if (sk_GENERAL_NAME_value (names, i)->type == GEN_DNS)
			dns = TRUE;
	GENERAL_NAMES_free (names);
	return dns;
}

/* 0 when cert matches host, -1 when it doesn't, -2 on a malformed (e.g.
   NUL-embedded) name. OpenSSL does the RFC 6125 matching: SANs first, the
   CN only when there are no DNS SANs, no partial wildcards. */
int
_SSL_check_hostname (X509 *cert, const char *host)
{
	GInetAddress *addr;
	int rv;

	if (cert == NULL || host == NULL)
		return -1;

	/* deprecated only as of OpenSSL 4.1, in favour of handshake-time
	   checks through X509_VERIFY_PARAM; this runs after the handshake so
	   ssl_do_connect can report (or ignore) a mismatch on its own */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	if ((addr = g_inet_address_new_from_string (host)) != NULL)
	{
		g_object_unref (addr);
		rv = X509_check_ip_asc (cert, host, 0);
	}
	else
		rv = X509_check_host (cert, host, 0,
							 X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS |
							 (cert_has_dns_san (cert) ? 0 : X509_CHECK_FLAG_ALWAYS_CHECK_SUBJECT), NULL);
G_GNUC_END_IGNORE_DEPRECATIONS

	return rv == 1 ? 0 : rv == 0 ? -1 : -2;
}
