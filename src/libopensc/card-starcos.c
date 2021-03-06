
/*
 * card-starcos.c: Support for STARCOS SPK 2.3 cards
 *
 * Copyright (C) 2003  Jörn Zukowski <zukowski@trustcenter.de> and 
 *                     Nils Larsch   <larsch@trustcenter.de>, TrustCenter AG
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "asn1.h"
#include "cardctl.h"
#include "internal.h"
#include "iso7816.h"

static struct sc_atr_table starcos_atrs[] = {
	{ "3B:B7:94:00:c0:24:31:fe:65:53:50:4b:32:33:90:00:b4", NULL, NULL, SC_CARD_TYPE_STARCOS_GENERIC, 0, NULL },
	{ "3B:B7:94:00:81:31:fe:65:53:50:4b:32:33:90:00:d1", NULL, NULL, SC_CARD_TYPE_STARCOS_GENERIC, 0, NULL },
	{ "3b:b7:18:00:c0:3e:31:fe:65:53:50:4b:32:34:90:00:25", NULL, NULL, SC_CARD_TYPE_STARCOS_GENERIC, 0, NULL },
	/* STARCOS 3.2 */
	{ "3b:9f:96:81:b1:fe:45:1f:07:00:64:05:1e:b2:00:31:b0:73:96:21:db:05:90:00:5c", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_2, 0, NULL },
	/* STARCOS 3.4 */
	{ "3b:d8:18:ff:81:b1:fe:45:1f:03:80:64:04:1a:b4:03:81:05:61", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_4, 0, NULL },
	{ NULL, NULL, NULL, 0, 0, NULL }
};

static struct sc_card_operations starcos_ops;
static struct sc_card_operations *iso_ops = NULL;

static struct sc_card_driver starcos_drv = {
	"STARCOS SPK 2.3/2.4/3.2/3.4",
	"starcos",
	&starcos_ops,
	NULL, 0, NULL
};

static const struct sc_card_error starcos_errors[] = 
{
	{ 0x6600, SC_ERROR_INCORRECT_PARAMETERS, "Error setting the security env"},
	{ 0x66F0, SC_ERROR_INCORRECT_PARAMETERS, "No space left for padding"},
	{ 0x69F0, SC_ERROR_NOT_ALLOWED,          "Command not allowed"},
	{ 0x6A89, SC_ERROR_FILE_ALREADY_EXISTS,  "Files exists"},
	{ 0x6A8A, SC_ERROR_FILE_ALREADY_EXISTS,  "Application exists"},
	{ 0x6F01, SC_ERROR_CARD_CMD_FAILED, "public key not complete"},
	{ 0x6F02, SC_ERROR_CARD_CMD_FAILED, "data overflow"},
	{ 0x6F03, SC_ERROR_CARD_CMD_FAILED, "invalid command sequence"},
	{ 0x6F05, SC_ERROR_CARD_CMD_FAILED, "security environment invalid"},
	{ 0x6F07, SC_ERROR_FILE_NOT_FOUND, "key part not found"},
	{ 0x6F08, SC_ERROR_CARD_CMD_FAILED, "signature failed"},
	{ 0x6F0A, SC_ERROR_INCORRECT_PARAMETERS, "key format does not match key length"},
	{ 0x6F0B, SC_ERROR_INCORRECT_PARAMETERS, "length of key component inconsistent with algorithm"},
	{ 0x6F81, SC_ERROR_CARD_CMD_FAILED, "system error"}
};

/* internal structure to save the current security environment */
typedef struct starcos_ex_data_st {
	int    sec_ops;	/* the currently selected security operation,
			 * i.e. SC_SEC_OPERATION_AUTHENTICATE etc. */
	unsigned int    fix_digestInfo;
} starcos_ex_data;

//CHECK_SUPPORTED_V3_4
int is_starcos_v3_4(sc_card_t *card)
{
	if (card->type != SC_CARD_TYPE_STARCOS_V3_4) {
		return 0;
	}
	return 1;
}


int is_starcos_v3_2(sc_card_t *card)
{
	if (card->type == SC_CARD_TYPE_STARCOS_V3_2) {
		return 1;
	} return 0;
}


/*Finds appropriate alg based on sec environment and writes these into the ptr
* returns number of bytes which were added, 0 if no alg specified. -1 if not supported
* The smartcard will use some standard algs (see "standard" comment) if no alg is defined */
int starcos_find_algorithm_flags_3_2(sc_card_t *card, const sc_security_env_t *env, u8 *p){
	sc_context_t *ctx = card->ctx;
	if (env->flags & SC_SEC_ENV_ALG_REF_PRESENT) {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Found alg ref id%02x\n",env->algorithm_ref & 0xFF );
		*p++ = 0x80;
		*p++ = 0x01;
		*p++ = env->algorithm_ref & 0xFF;
		return 3;
	} else {
		switch (((starcos_ex_data *)card->drv_data)->sec_ops) {
			case SC_SEC_OPERATION_DECIPHER: // encipher algorithms used here (see starcos manual)
				if(env->algorithm == SC_ALGORITHM_RSA){
					*p++= 0x89;
					*p++= 0x02;
					*p++= 0x11;//encipher
					*p++= 0x30;//rsa (standard)
					return 4;
				}
				if(env->algorithm == SC_ALGORITHM_DES){
					return -1; //for now, not supported
					/**p++= 0x89;
					*p++= 0x02;
					*p++= 0x11;//encipher
					*p++= 0x11;//des
					*p++= 0x00; //Here: some modes missing (CBC, ICV)*/
				}
				if(env->algorithm == SC_ALGORITHM_3DES){
					return -1; //for now, not supported
					/**p++= 0x89;
					*p++= 0x02;
					*p++= 0x11;//encipher
					*p++= 0x21;//3des
					*p++= 0x00; //Here: some modes missing (CBC, ICV)*/
				}
				return 0;
			case SC_SEC_OPERATION_SIGN:
				if (env->algorithm_flags & SC_ALGORITHM_RSA_PAD_PKCS1) {
					*p++= 0x89;
					*p++= 0x02;
					*p++= 0x13;//signature
					*p++= 0x23;//PKCS RSA (standard)
					if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_SHA1){
						//TODO: not tested yet
						*p++= 0x10;
						return 5;
					}
					if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_RIPEMD160){
						//TODO: not tested yet
						*p++= 0x20;
						return 5;
					}
					return 4;
				}
				//iso 9796-2 DINSIG
				if (env->algorithm_flags & SC_ALGORITHM_RSA_PAD_ISO9796) {
					return -1; //TODO: Not supported because not tested yet
					*p++= 0x89;
					*p++= 0x02;
					*p++= 0x13;//signature
					*p++= 0x13;//ISO 9796 and  RSA (standard)
					if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_SHA1){
						//TODO: not tested yet
						*p++= 0x10;
						return 5;
					}
					if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_RIPEMD160){
						//TODO: not tested yet
						*p++= 0x20;
						return 5;
					}
					return 4;
				}
				break;
			case SC_SEC_OPERATION_AUTHENTICATE:
				//asymmetric
				if(env->flags & SC_SEC_ENV_KEY_REF_ASYMMETRIC){
					/*according to manual implemented using client-server authentication?*/
					if(env->algorithm_flags & SC_ALGORITHM_RSA_PADS){ //RSA
						*p++= 0x89;
						*p++= 0x02;
						*p++= 0x23;//asymmetric authentication
						*p++= 0x13;// client-server with RSA (standard)a
						return 4;
					} else {
						/*TODO: NOT supported yet: client-server with with ECC (eliptic curve cryptography?)
						if(ecc -> 2324)	*/
						return -1;
						*p++= 0x89;
						*p++= 0x02;
						*p++= 0x23;//asymmetric authentication
						*p++= 0x24;// client-server with ECC
					}
					/*internal authenticate ICAO(International Civil Aviation Organisation)?, with RSA (standard)
					if(internal authenticate ICAO and rsa){
						*p++= 0x89;
						*p++= 0x02;
						*p++= 0x23;//asymmetric authentication
						*p++= 0x53;// internal authentication ICAO (??)
						if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_SHA1){
							//TODO: not tested yet
							*p++= 0x10;
							return 5;
						}
						return 4;*/
				} else {
					/* For now, signature sometimes runs into this case -> return 0, not -1 to continue signature procedures
					* Not supported yet
					* TODO: check one-sided symmetric
					*		double-sided symmetric */
					return 0;
				}
				break;
			default:	//don*t do anything ?
				return 0;
		}
	}
	return 0;
}

/* the starcos part */
static int starcos_match_card(sc_card_t *card)
{
	int i;

	i = _sc_match_atr(card, starcos_atrs, &card->type);
	if (i < 0)
		return 0;
	return 1;
}

static int starcos_init(sc_card_t *card)
{
	unsigned int flags;
	starcos_ex_data *ex_data;
	ex_data = calloc(1, sizeof(starcos_ex_data));
	if (ex_data == NULL)
		return SC_ERROR_OUT_OF_MEMORY;

	card->name = "STARCOS SPK 2.3";
	card->cla  = 0x00;
	card->drv_data = (void *)ex_data;

	flags = SC_ALGORITHM_RSA_PAD_PKCS1
		| SC_ALGORITHM_ONBOARD_KEY_GEN
		| SC_ALGORITHM_RSA_PAD_ISO9796
		| SC_ALGORITHM_RSA_HASH_NONE
		| SC_ALGORITHM_RSA_HASH_SHA1
		| SC_ALGORITHM_RSA_HASH_MD5
		| SC_ALGORITHM_RSA_HASH_RIPEMD160
		| SC_ALGORITHM_RSA_HASH_MD5_SHA1;

	card->caps = SC_CARD_CAP_RNG; 

	if (is_starcos_v3_4(card)  || is_starcos_v3_2(card) ) {
		if(is_starcos_v3_2(card)){
			card->name= "STARCOS SPK 3.2";
			card->caps |= SC_CARD_CAP_ISO7816_PIN_INFO;
		}else{
			card->name = "STARCOS SPK 3.4";
		}
		flags |= SC_CARD_FLAG_RNG
			| SC_ALGORITHM_RSA_HASH_SHA224
			| SC_ALGORITHM_RSA_HASH_SHA256
			| SC_ALGORITHM_RSA_HASH_SHA384
			| SC_ALGORITHM_RSA_HASH_SHA512;

		_sc_card_add_rsa_alg(card, 512, flags, 0x10001);
		_sc_card_add_rsa_alg(card, 768, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1024, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1728, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1976, flags, 0x10001);
		_sc_card_add_rsa_alg(card,2048, flags, 0x10001);
	} else {
		_sc_card_add_rsa_alg(card, 512, flags, 0x10001);
		_sc_card_add_rsa_alg(card, 768, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1024, flags, 0x10001);

		/* we need read_binary&friends with max 128 bytes per read */
		card->max_send_size = 128;
		card->max_recv_size = 128;
	}
	//TODO: for starcos 3_2, the file (id 3f002f01) cannot be found -> not existent? <-> 3f002f02 == EF_GDO (not present either)
	if (sc_parse_ef_atr(card) == SC_SUCCESS) {
		if (card->ef_atr->card_capabilities & ISO7816_CAP_EXTENDED_LENGTH) {
			card->caps |= SC_CARD_CAP_APDU_EXT;
		}
		if (card->ef_atr->max_response_apdu > 0) {
			card->max_recv_size = card->ef_atr->max_response_apdu;
		}
		if (card->ef_atr->max_command_apdu > 0) {
			card->max_send_size = card->ef_atr->max_command_apdu;
		}
	}
	return 0;
}

static int starcos_finish(sc_card_t *card)
{
	if (card->drv_data)
		free((starcos_ex_data *)card->drv_data);
	return 0;
}

static int process_fci(sc_context_t *ctx, sc_file_t *file,
		       const u8 *buf, size_t buflen)
{
	/* NOTE: According to the Starcos S 2.1 manual it's possible
	 *       that a SELECT DF returns as a FCI arbitrary data which
	 *       is stored in a object file (in the corresponding DF)
	 *       with the tag 0x6f.
	 */

	size_t taglen, len = buflen;
	const u8 *tag = NULL, *p;
  
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "processing FCI bytes\n");

	if (buflen < 2)
		return SC_ERROR_INTERNAL;
	if (buf[0] != 0x6f)
		return SC_ERROR_INVALID_DATA;
	len = (size_t)buf[1];
	if (buflen - 2 < len)
		return SC_ERROR_INVALID_DATA;
	p = buf + 2;

	/* defaults */
	file->type = SC_FILE_TYPE_WORKING_EF;
	file->ef_structure = SC_FILE_EF_UNKNOWN;
	file->shareable = 0;
	file->record_length = 0;
	file->size = 0;
  
	tag = sc_asn1_find_tag(ctx, p, len, 0x80, &taglen);
	if (tag != NULL && taglen >= 2) {
		int bytes = (tag[0] << 8) + tag[1];
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
			"  bytes in file: %d\n", bytes);
		file->size = bytes;
	}

  	tag = sc_asn1_find_tag(ctx, p, len, 0x82, &taglen);
	if (tag != NULL) {
		const char *type = "unknown";
		const char *structure = "unknown";

		if (taglen == 1 && tag[0] == 0x01) {
			/* transparent EF */
			type = "working EF";
			structure = "transparent";
			file->type = SC_FILE_TYPE_WORKING_EF;
			file->ef_structure = SC_FILE_EF_TRANSPARENT;
		} else if (taglen == 1 && tag[0] == 0x11) {
			/* object EF */
			type = "working EF";
			structure = "object";
			file->type = SC_FILE_TYPE_WORKING_EF;
			file->ef_structure = SC_FILE_EF_TRANSPARENT; /* TODO */
		} else if (taglen == 3 && tag[1] == 0x21) {
			type = "working EF";
			file->record_length = tag[2];
			file->type = SC_FILE_TYPE_WORKING_EF;
			/* linear fixed, cyclic or compute */
			switch ( tag[0] )
			{
				case 0x02:
					structure = "linear fixed";
					file->ef_structure = SC_FILE_EF_LINEAR_FIXED;
					break;
				case 0x07:
					structure = "cyclic";
					file->ef_structure = SC_FILE_EF_CYCLIC;
					break;
				case 0x17:
					structure = "compute";
					file->ef_structure = SC_FILE_EF_UNKNOWN;
					break;
				default:
					structure = "unknown";
					file->ef_structure = SC_FILE_EF_UNKNOWN;
					file->record_length = 0;
					break;
			}
		}

 		sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
			"  type: %s\n", type);
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
			"  EF structure: %s\n", structure);
	}
	file->magic = SC_FILE_MAGIC;

	return SC_SUCCESS;
}

static int process_fci_v3_4(sc_context_t *ctx, sc_file_t *file,
		       const u8 *buf, size_t buflen)
{
	size_t taglen, len = buflen;
	const u8 *tag = NULL, *p;

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
		 "processing %"SC_FORMAT_LEN_SIZE_T"u FCI bytes\n", buflen);

	if (buflen < 2)
		return SC_ERROR_INTERNAL;
	if (buf[0] != 0x6f)
		return SC_ERROR_INVALID_DATA;
	len = (size_t)buf[1];
	if (buflen - 2 < len)
		return SC_ERROR_INVALID_DATA;

	/* defaults */
	file->type = SC_FILE_TYPE_WORKING_EF;
	if (len == 0) {
		SC_FUNC_RETURN(ctx, 2, SC_SUCCESS);
	}

	p = buf + 2;
	file->ef_structure = SC_FILE_TYPE_DF;
	file->shareable = 1;
	tag = sc_asn1_find_tag(ctx, p, len, 0x84, &taglen);
	if (tag != NULL && taglen > 0 && taglen <= 16) {
		memcpy(file->name, tag, taglen);
		file->namelen = taglen;
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "filename %s",
			sc_dump_hex(file->name, file->namelen));
	}
	return SC_SUCCESS;
}

static int process_fcp_v3_4(sc_context_t *ctx, sc_file_t *file,
		       const u8 *buf, size_t buflen)
{
	size_t taglen, len = buflen;
	const u8 *tag = NULL, *p;
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
		 "processing %"SC_FORMAT_LEN_SIZE_T"u FCP bytes\n", buflen);

	if (buflen < 2)
		return SC_ERROR_INTERNAL;
	if (buf[0] != 0x62)
		return SC_ERROR_INVALID_DATA;
	len = (size_t)buf[1];
	if (buflen - 2 < len)
		return SC_ERROR_INVALID_DATA;
	p = buf + 2;

	tag = sc_asn1_find_tag(ctx, p, len, 0x80, &taglen);
	if (tag != NULL && taglen >= 2) {
		int bytes = (tag[0] << 8) + tag[1];
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
			"  bytes in file: %d\n", bytes);
		file->size = bytes;
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0xc5, &taglen);
	if (tag != NULL && taglen >= 2) {
		int bytes = (tag[0] << 8) + tag[1];
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
			"  bytes in file 2: %d\n", bytes);
		file->size = bytes;
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x82, &taglen);
	if (tag != NULL) {
		const char *type = "unknown";
		const char *structure = "unknown";

		if (taglen >= 1) {
			unsigned char byte = tag[0];
			if (byte & 0x40) {
				file->shareable = 1;
			}
			if (byte == 0x38) {
				type = "DF";
				file->type = SC_FILE_TYPE_DF;
				file->shareable = 1;
			}
			switch (byte & 7) {
			case 1:
				/* transparent EF */
				type = "working EF";
				structure = "transparent";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_TRANSPARENT;
				break;
			case 2:
				/* linear fixed EF */
				type = "working EF";
				structure = "linear fixed";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_LINEAR_FIXED;
				break;
			case 4:
				/* linear variable EF */
				type = "working EF";
				structure = "linear variable";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_LINEAR_VARIABLE;
				break;
			case 6:
				/* cyclic EF */
				type = "working EF";
				structure = "cyclic";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_CYCLIC;
				break;
			default:
				/* use defaults from above */
				break;
			}
		}
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
			"  type: %s\n", type);
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
			"  EF structure: %s\n", structure);
		if (taglen >= 2) {
			if (tag[1] != 0x41 || taglen != 5) {
				SC_FUNC_RETURN(ctx, 2,SC_ERROR_INVALID_DATA);
			}
			/* formatted EF */
			file->record_length = (tag[2] << 8) + tag[3];
			file->record_count = tag[4];
			sc_debug(ctx, SC_LOG_DEBUG_NORMAL,
				"  rec_len: %d  rec_cnt: %d\n\n",
				file->record_length, file->record_count);
		}
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x83, &taglen);
	if (tag != NULL && taglen >= 2) {
		file->id = (tag[0] << 8) | tag[1];
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "  file identifier: 0x%02X%02X\n",
			tag[0], tag[1]);
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x84, &taglen);
	if (tag != NULL && taglen > 0 && taglen <= 16) {
		memcpy(file->name, tag, taglen);
		file->namelen = taglen;
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "  filename %s",
			sc_dump_hex(file->name, file->namelen));
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x8a, &taglen);
	if (tag != NULL && taglen == 1) {
		char* status = "unknown";
		switch (tag[0]) {
		case 1:
			status = "creation";
			file->status = SC_FILE_STATUS_CREATION;
			break;
		case 5:
			status = "operational active";
			file->status = SC_FILE_STATUS_ACTIVATED;
			break;
		case 12:
		case 13:
			status = "creation";
			file->status = SC_FILE_STATUS_INVALIDATED;
			break;
		default:
			break;
		}
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "  file status: %s\n", status);
	}

	file->magic = SC_FILE_MAGIC;
	return SC_SUCCESS;
}


/*used to select DF/EF/MF with given aid*/
static int starcos_select_aid(sc_card_t *card,
			      u8 aid[16], size_t len,
			      sc_file_t **file_out)
{
	sc_apdu_t apdu;
	int r;
	size_t i = 0;
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xA4, 0x04, 0x0C);
	apdu.lc = len;
	apdu.data = (u8*)aid;
	apdu.datalen = len;
	apdu.resplen = 0;
	apdu.le = 0;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");

	/* check return value */
	if (!(apdu.sw1 == 0x90 && apdu.sw2 == 0x00) && apdu.sw1 != 0x61 )
    		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));
  
	/* update cache */
	card->cache.current_path.type = SC_PATH_TYPE_DF_NAME;
	card->cache.current_path.len = len;
	memcpy(card->cache.current_path.value, aid, len);

	if (file_out) {
		sc_file_t *file = sc_file_new();
		if (!file)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY);
		file->type = SC_FILE_TYPE_DF;
		file->ef_structure = SC_FILE_EF_UNKNOWN;
		file->path.len = 0;
		file->size = 0;
		/* AID */
		for (i = 0; i < len; i++)
			file->name[i] = aid[i];
		file->namelen = len;
		file->id = 0x0000;
		file->magic = SC_FILE_MAGIC;
		*file_out = file;
	}
	SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
}

static int starcos_select_fid(sc_card_t *card,
			      unsigned int id_hi, unsigned int id_lo,
			      sc_file_t **file_out, int is_file)
{
	sc_apdu_t apdu;
	u8 data[] = {id_hi & 0xff, id_lo & 0xff};
	u8 resp[SC_MAX_APDU_BUFFER_SIZE];
	int bIsDF = 0, r;
	int isFCP = 0;
	int isMF = 0;

	/* request FCI to distinguish between EFs and DFs */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0xA4, 0x00, 0x00);
	apdu.p2   = 0x00;
	apdu.resp = resp;
	apdu.resplen = SC_MAX_APDU_BUFFER_SIZE;
	apdu.le = 256;
	apdu.lc = 2;
	apdu.data = data;
	apdu.datalen = 2;

	if (is_starcos_v3_4(card) || is_starcos_v3_2(card)) {
		if (id_hi == 0x3f && id_lo == 0x0) {
			apdu.p1 = 0x0; //MF
			apdu.p2 = 0x0; //return fci
			isMF = 1;
			isFCP = 0;
		} else if (file_out || is_file) {
			// last component (i.e. file or path)
			apdu.p1 = 0x2; //ef
			apdu.p2 = 0x4; // return fcp
			bIsDF = 0;
			isFCP = 1;
		} else {
			// path component
			apdu.p1 = 0x1; // df
			apdu.p2 = 0x0; //return fci
			bIsDF = 1;
			isFCP = 0;
		}
	}
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");

	//check response for starcos 3.2/3.4
	if (is_starcos_v3_4(card)|| is_starcos_v3_2(card)) {
		 if (apdu.p2 == 0x4 && apdu.sw1 == 0x6a && apdu.sw2 == 0x82) { //error file not found
			/* not a file, could be a path */
			bIsDF = 1;
			isFCP = 0;
			apdu.p1 = 0x1; //df
			apdu.p2 = 0x0; //return fci
			apdu.resplen = sizeof(resp);
			apdu.le = 256;
			apdu.lc = 2;
			r = sc_transmit_apdu(card, &apdu);
			SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU re-transmit failed");
		}
	}
	else{ //older starcos versions
		if (apdu.p2 == 0x00 && apdu.sw1 == 0x62 && apdu.sw2 == 0x84 ) {
			/* no FCI => we have a DF (see comment in process_fci()) */
			bIsDF = 1;
			apdu.p2 = 0x0C;
			apdu.cse = SC_APDU_CASE_3_SHORT;
			apdu.resplen = 0;
			apdu.le = 0;
			r = sc_transmit_apdu(card, &apdu);
			SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU re-transmit failed");
		}
		else if (apdu.sw1 == 0x61 || (apdu.sw1 == 0x90 && apdu.sw2 == 0x00 && !isMF)) {
			/* SELECT returned some data (possible FCI) =>
			 * try a READ BINARY to see if a EF is selected */
			sc_apdu_t apdu2;
			u8 resp2[2];
			sc_format_apdu(card, &apdu2, SC_APDU_CASE_2_SHORT, 0xB0, 0, 0);
			apdu2.resp = (u8*)resp2;
			apdu2.resplen = 2;
			apdu2.le = 1;
			apdu2.lc = 0;
			r = sc_transmit_apdu(card, &apdu2);
			SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
			if (apdu2.sw1 == 0x69 && apdu2.sw2 == 0x86) {
				/* no current EF is selected => we have a DF */
				bIsDF = 1;
			} else {
				isFCP = 1;
			}
		}
	}

	if (apdu.sw1 != 0x61 && (apdu.sw1 != 0x90 || apdu.sw2 != 0x00))
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));

	/* update cache */
	if (bIsDF || isMF) {
		card->cache.current_path.type = SC_PATH_TYPE_PATH;
		card->cache.current_path.value[0] = 0x3f;
		card->cache.current_path.value[1] = 0x00;
		if (id_hi == 0x3f && id_lo == 0x00)
			card->cache.current_path.len = 2;
		else {
			card->cache.current_path.len = 4;
			card->cache.current_path.value[2] = id_hi;
			card->cache.current_path.value[3] = id_lo;
		}
	}

	if (file_out) {
		sc_file_t *file = sc_file_new();
		if (!file)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY);
		file->id   = (id_hi << 8) + id_lo;
		file->path = card->cache.current_path;

		if (bIsDF) {
			/* we have a DF */
			file->type = SC_FILE_TYPE_DF;
			file->ef_structure = SC_FILE_EF_UNKNOWN;
			file->size = 0;
			file->namelen = 0;
			file->magic = SC_FILE_MAGIC;
			*file_out = file;
		} else {
			/* ok, assume we have a EF */
			if (is_starcos_v3_4(card) || is_starcos_v3_2(card)) {
				if (isFCP) {
					r = process_fcp_v3_4(card->ctx, file, apdu.resp,
							apdu.resplen);
				} else {
					r = process_fci_v3_4(card->ctx, file, apdu.resp,
							apdu.resplen);
				}
			} else {
				r = process_fci(card->ctx, file, apdu.resp,
						apdu.resplen);
			}
			if (r != SC_SUCCESS) {
				sc_file_free(file);
				return r;
			}

			*file_out = file;
		}
	}

	SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
}

static int starcos_select_file(sc_card_t *card,
			       const sc_path_t *in_path,
			       sc_file_t **file_out)
{
	u8 pathbuf[SC_MAX_PATH_SIZE], *path = pathbuf;
	int    r;
	size_t i, pathlen;
	char pbuf[SC_MAX_PATH_STRING_SIZE];

	SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_VERBOSE);

	r = sc_path_print(pbuf, sizeof(pbuf), &card->cache.current_path);
	if (r != SC_SUCCESS)
		pbuf[0] = '\0';

	sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
		 "current path (%s, %s): %s (len: %"SC_FORMAT_LEN_SIZE_T"u)\n",
		 card->cache.current_path.type == SC_PATH_TYPE_DF_NAME ?
		 "aid" : "path",
		 card->cache.valid ? "valid" : "invalid", pbuf,
		 card->cache.current_path.len);

	memcpy(path, in_path->value, in_path->len);
	pathlen = in_path->len;

	if (in_path->type == SC_PATH_TYPE_FILE_ID)
	{	/* SELECT EF/DF with ID */
		/* Select with 2byte File-ID */
		if (pathlen != 2)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE,SC_ERROR_INVALID_ARGUMENTS);
		return starcos_select_fid(card, path[0], path[1], file_out, 1);
	}
	else if (in_path->type == SC_PATH_TYPE_DF_NAME)
      	{	/* SELECT DF with AID */
		/* Select with 1-16byte Application-ID */
		if (card->cache.valid
		    && card->cache.current_path.type == SC_PATH_TYPE_DF_NAME
		    && card->cache.current_path.len == pathlen
		    && memcmp(card->cache.current_path.value, pathbuf, pathlen) == 0 )
		{
			sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL, "cache hit\n");
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
		}
		else
			return starcos_select_aid(card, pathbuf, pathlen, file_out);
	}
	else if (in_path->type == SC_PATH_TYPE_PATH)
	{
		u8 n_pathbuf[SC_MAX_PATH_SIZE];
		int bMatch = -1;

		/* Select with path (sequence of File-IDs) */
		/* Starcos (S 2.1 and SPK 2.3) only supports one
		 * level of subdirectories, therefore a path is
		 * at most 3 FID long (the last one being the FID
		 * of a EF) => pathlen must be even and less than 6
		 */
		if (pathlen%2 != 0 || pathlen > 6 || pathlen <= 0)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);
		/* if pathlen == 6 then the first FID must be MF (== 3F00) */
		if (pathlen == 6 && ( path[0] != 0x3f || path[1] != 0x00 ))
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);

		if ( !is_starcos_v3_4(card) ||
		    (pathlen == 0 && card->cache.current_path.type != SC_PATH_TYPE_DF_NAME)) {
			/* unify path (the first FID should be MF) */
			if (path[0] != 0x3f || path[1] != 0x00)
			{
				n_pathbuf[0] = 0x3f;
				n_pathbuf[1] = 0x00;
				for (i=0; i< pathlen; i++)
					n_pathbuf[i+2] = pathbuf[i];
				path = n_pathbuf;
				pathlen += 2;
			}
		}

		/* check current working directory */
		if (card->cache.valid
		    && card->cache.current_path.type == SC_PATH_TYPE_PATH
		    && card->cache.current_path.len >= 2
		    && card->cache.current_path.len <= pathlen )
		{
			bMatch = 0;
			for (i=0; i < card->cache.current_path.len; i+=2)
				if (card->cache.current_path.value[i] == path[i]
				    && card->cache.current_path.value[i+1] == path[i+1] )
					bMatch += 2;

			if ((is_starcos_v3_4(card)|| is_starcos_v3_2(card)) &&
			    bMatch > 0 &&
			    (size_t) bMatch < card->cache.current_path.len) {
				/* we're in the wrong folder, start traversing from root */
				bMatch = 0;
				card->cache.current_path.len = 0;
			}
		}

		if ( card->cache.valid && bMatch >= 0 )
		{
			if ( pathlen - bMatch == 2 )
				/* we are in the rigth directory */
				return starcos_select_fid(card, path[bMatch], path[bMatch+1], file_out, 1);
			else if ( pathlen - bMatch > 2 )
			{
				/* two more steps to go */
				sc_path_t new_path;

				/* first step: change directory */
				r = starcos_select_fid(card, path[bMatch], path[bMatch+1], NULL, 0);
				SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "SELECT FILE (DF-ID) failed");

				memset(&new_path, 0, sizeof(sc_path_t));
				new_path.type = SC_PATH_TYPE_PATH;
				new_path.len  = pathlen - bMatch-2;
				memcpy(new_path.value, &(path[bMatch+2]), new_path.len);
				/* final step: select file */
				return starcos_select_file(card, &new_path, file_out);
      			}
			else /* if (bMatch - pathlen == 0) */
			{
				/* done: we are already in the
				 * requested directory */
				sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
					"cache hit\n");
				/* copy file info (if necessary) */
				if (file_out) {
					sc_file_t *file = sc_file_new();
					if (!file)
						SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY);
					file->id = (path[pathlen-2] << 8) +
						   path[pathlen-1];
					file->path = card->cache.current_path;
					file->type = SC_FILE_TYPE_DF;
					file->ef_structure = SC_FILE_EF_UNKNOWN;
					file->size = 0;
					file->namelen = 0;
					file->magic = SC_FILE_MAGIC;
					*file_out = file;
				}
				/* nothing left to do */
				return SC_SUCCESS;
			}
		}
		else
		{
			/* no usable cache */
			for ( i=0; i<pathlen-2; i+=2 )
			{
				r = starcos_select_fid(card, path[i], path[i+1], NULL, 0);
				SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "SELECT FILE (DF-ID) failed");
			}
			return starcos_select_fid(card, path[pathlen-2], path[pathlen-1], file_out, 1);
		}
	}
	else
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);
}

#define STARCOS_AC_ALWAYS	0x9f
#define STARCOS_AC_NEVER	0x5f
#define STARCOS_PINID2STATE(a)	((((a) & 0x0f) == 0x01) ? ((a) & 0x0f) : (0x0f - ((0x0f & (a)) >> 1)))

static u8 process_acl_entry(sc_file_t *in, unsigned int method, unsigned int in_def)
{
	u8 def = (u8)in_def;
	const sc_acl_entry_t *entry = sc_file_get_acl_entry(in, method);
	if (!entry)
		return def;
	else if (entry->method & SC_AC_CHV) {
		unsigned int key_ref = entry->key_ref;
		if (key_ref == SC_AC_KEY_REF_NONE)
			return def;
		else if ((key_ref & 0x0f) == 1)
			/* SOPIN */
			return (key_ref & 0x80 ? 0x10 : 0x00) | 0x01;
		else
			return (key_ref & 0x80 ? 0x10 : 0x00) | STARCOS_PINID2STATE(key_ref);
	} else if (entry->method & SC_AC_NEVER)
		return STARCOS_AC_NEVER;
	else
		return def;
}

/** starcos_process_acl
 * \param card pointer to the sc_card object
 * \param file pointer to the sc_file object
 * \param data pointer to a sc_starcos_create_data structure
 * \return SC_SUCCESS if no error occured otherwise error code
 *
 * This function tries to create a somewhat useable Starcos spk 2.3 acl
 * from the OpenSC internal acl (storing the result in the supplied
 * sc_starcos_create_data structure).
 */
static int starcos_process_acl(sc_card_t *card, sc_file_t *file,
	sc_starcos_create_data *data)
{
	u8     tmp, *p;
	static const u8 def_key[] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};

	if (file->type == SC_FILE_TYPE_DF && file->id == 0x3f00) {
		p    = data->data.mf.header;
		memcpy(p, def_key, 8);
		p   += 8;
		*p++ = (file->size >> 8) & 0xff;
		*p++ = file->size & 0xff;
		/* guess isf size (mf_size / 4) */
		*p++ = (file->size >> 10) & 0xff;
		*p++ = (file->size >> 2)  & 0xff;
		/* ac create ef  */
		*p++ = process_acl_entry(file,SC_AC_OP_CREATE,STARCOS_AC_ALWAYS);
		/* ac create key */
		*p++ = process_acl_entry(file,SC_AC_OP_CREATE,STARCOS_AC_ALWAYS);
		/* ac create df  */
		*p++ = process_acl_entry(file,SC_AC_OP_CREATE,STARCOS_AC_ALWAYS);
		/* use the same ac for register df and create df */
		*p++ = data->data.mf.header[14];
		/* if sm is required use combined mode */
		if (file->acl[SC_AC_OP_CREATE] && (sc_file_get_acl_entry(file, SC_AC_OP_CREATE))->method & SC_AC_PRO)
			tmp = 0x03;	/* combinde mode */
		else
			tmp = 0x00;	/* no sm */
		*p++ = tmp;	/* use the same sm mode for all ops */
		*p++ = tmp;
		*p = tmp;
		data->type = SC_STARCOS_MF_DATA;

		return SC_SUCCESS;
	} else if (file->type == SC_FILE_TYPE_DF){
		p    = data->data.df.header;
		*p++ = (file->id >> 8) & 0xff;
		*p++ = file->id & 0xff;
		if (file->namelen) {
			/* copy aid */
			*p++ = file->namelen & 0xff;
			memset(p, 0, 16);
			memcpy(p, file->name, (u8)file->namelen);
			p   += 16;
		} else {
			/* (mis)use the fid as aid */
			*p++ = 2;
			memset(p, 0, 16);
			*p++ = (file->id >> 8) & 0xff;
			*p++ = file->id & 0xff;
			p   += 14;
		}
		/* guess isf size */
		*p++ = (file->size >> 10) & 0xff;	/* ISF space */
		*p++ = (file->size >> 2)  & 0xff;	/* ISF space */
		/* ac create ef  */
		*p++ = process_acl_entry(file,SC_AC_OP_CREATE,STARCOS_AC_ALWAYS);
		/* ac create key */
		*p++ = process_acl_entry(file,SC_AC_OP_CREATE,STARCOS_AC_ALWAYS);
		/* set sm byte (same for keys and ef) */
		if (file->acl[SC_AC_OP_CREATE] &&
		    (sc_file_get_acl_entry(file, SC_AC_OP_CREATE)->method &
		     SC_AC_PRO))
			tmp = 0x03;
		else
			tmp = 0x00;
		*p++ = tmp;	/* SM CR  */
		*p = tmp;	/* SM ISF */

		data->data.df.size[0] = (file->size >> 8) & 0xff;
		data->data.df.size[1] = file->size & 0xff;
		data->type = SC_STARCOS_DF_DATA;

		return SC_SUCCESS;
	} else if (file->type == SC_FILE_TYPE_WORKING_EF) {
		p    = data->data.ef.header;
		*p++ = (file->id >> 8) & 0xff;
		*p++ = file->id & 0xff;
		/* ac read  */
		*p++ = process_acl_entry(file, SC_AC_OP_READ,STARCOS_AC_ALWAYS);
		/* ac write */
		*p++ = process_acl_entry(file, SC_AC_OP_WRITE,STARCOS_AC_ALWAYS);
		/* ac erase */
		*p++ = process_acl_entry(file, SC_AC_OP_ERASE,STARCOS_AC_ALWAYS);
		*p++ = STARCOS_AC_ALWAYS;	/* AC LOCK     */
		*p++ = STARCOS_AC_ALWAYS;	/* AC UNLOCK   */
		*p++ = STARCOS_AC_ALWAYS;	/* AC INCREASE */
		*p++ = STARCOS_AC_ALWAYS;	/* AC DECREASE */
		*p++ = 0x00;			/* rfu         */
		*p++ = 0x00;			/* rfu         */
		/* use sm (in combined mode) if wanted */
		if ((file->acl[SC_AC_OP_READ]   && (sc_file_get_acl_entry(file, SC_AC_OP_READ)->method & SC_AC_PRO)) ||
		    (file->acl[SC_AC_OP_UPDATE] && (sc_file_get_acl_entry(file, SC_AC_OP_UPDATE)->method & SC_AC_PRO)) ||
		    (file->acl[SC_AC_OP_WRITE]  && (sc_file_get_acl_entry(file, SC_AC_OP_WRITE)->method & SC_AC_PRO)) )
			tmp = 0x03;
		else
			tmp = 0x00;
		*p++ = tmp;			/* SM byte     */
		*p++ = 0x00;			/* use the least significant 5 bits
					 	 * of the FID as SID */
		switch (file->ef_structure)
		{
		case SC_FILE_EF_TRANSPARENT:
			*p++ = 0x81;
			*p++ = (file->size >> 8) & 0xff;
			*p = file->size & 0xff;
			break;
		case SC_FILE_EF_LINEAR_FIXED:
			*p++ = 0x82;
			*p++ = file->record_count  & 0xff;
			*p = file->record_length & 0xff;
			break;
		case SC_FILE_EF_CYCLIC:
			*p++ = 0x84;
			*p++ = file->record_count  & 0xff;
			*p = file->record_length & 0xff;
			break;
		default:
			return SC_ERROR_INVALID_ARGUMENTS;
		}
		data->type = SC_STARCOS_EF_DATA;

		return SC_SUCCESS;
	} else
                return SC_ERROR_INVALID_ARGUMENTS;
}

/** starcos_create_mf
 * internal function to create the MF
 * \param card pointer to the sc_card structure
 * \param data pointer to a sc_starcos_create_data object
 * \return SC_SUCCESS or error code
 *
 * This function creates the MF based on the information stored
 * in the sc_starcos_create_data.mf structure. Note: CREATE END must be
 * called separately to activate the ACs.
 */
static int starcos_create_mf(sc_card_t *card, sc_starcos_create_data *data)
{
	int    r;
	sc_apdu_t       apdu;
	sc_context_t   *ctx = card->ctx;

	if(is_starcos_v3_4(card) || is_starcos_v3_2(card)){
		sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
			"not supported for STARCOS 3.4/3.2 cards");
		return SC_ERROR_NOT_SUPPORTED;
	}
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "creating MF \n");
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xE0, 0x00, 0x00);
	apdu.cla |= 0x80;
	apdu.lc   = 19;
	apdu.datalen = 19;
	apdu.data = (u8 *) data->data.mf.header;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	return sc_check_sw(card, apdu.sw1, apdu.sw2);
}

/** starcos_create_df
 * internal function to create a DF
 * \param card pointer to the sc_card structure
 * \param data pointer to a sc_starcos_create_data object
 * \return SC_SUCCESS or error code
 *
 * This functions registers and creates a DF based in the information
 * stored in a sc_starcos_create_data.df data structure. Note: CREATE END must
 * be called separately to activate the ACs.
 */
static int starcos_create_df(sc_card_t *card, sc_starcos_create_data *data)
{
	int    r;
	size_t len;
	sc_apdu_t       apdu;
	sc_context_t   *ctx = card->ctx;
	if(is_starcos_v3_4(card) || is_starcos_v3_2(card)){
		sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
			"not supported for STARCOS 3.4/3.2 cards");
		return SC_ERROR_NOT_SUPPORTED;
	}
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "creating DF\n");
	/* first step: REGISTER DF */
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "calling REGISTER DF\n");

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x52,
		       data->data.df.size[0], data->data.df.size[1]);
	len  = 3 + data->data.df.header[2];
	apdu.cla |= 0x80;
	apdu.lc   = len;
	apdu.datalen = len;
	apdu.data = data->data.df.header;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	/* second step: CREATE DF */
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "calling CREATE DF\n");

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xE0, 0x01, 0x00);
	apdu.cla |= 0x80;
	apdu.lc   = 25;
	apdu.datalen = 25;
	apdu.data = data->data.df.header;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	return sc_check_sw(card, apdu.sw1, apdu.sw2);
}

/** starcos_create_ef
 * internal function to create a EF
 * \param card pointer to the sc_card structure
 * \param data pointer to a sc_starcos_create_data object
 * \return SC_SUCCESS or error code
 *
 * This function creates a EF based on the information stored in
 * the sc_starcos_create_data.ef data structure.
 */
static int starcos_create_ef(sc_card_t *card, sc_starcos_create_data *data)
{
	int    r;
	sc_apdu_t       apdu;
	sc_context_t   *ctx = card->ctx;


	if(is_starcos_v3_4(card) || is_starcos_v3_2(card)){
		sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
			"not supported for STARCOS 3.4/3.2 cards");
		return SC_ERROR_NOT_SUPPORTED;
	}
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "creating EF\n");

	sc_format_apdu(card,&apdu,SC_APDU_CASE_3_SHORT,0xE0,0x03,0x00);
	apdu.cla |= 0x80;
	apdu.lc   = 16;
	apdu.datalen = 16;
	apdu.data = (u8 *) data->data.ef.header;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	return sc_check_sw(card, apdu.sw1, apdu.sw2);
}

/** starcos_create_end
 * internal function to activate the ACs
 * \param card pointer to the sc_card structure
 * \param file pointer to a sc_file object
 * \return SC_SUCCESS or error code
 *
 * This function finishs the creation of a DF (or MF) and activates
 * the ACs.
 */
static int starcos_create_end(sc_card_t *card, sc_file_t *file)
{
	int r;
	u8  fid[2];
	sc_apdu_t       apdu;

	if (file->type != SC_FILE_TYPE_DF)
		return SC_ERROR_INVALID_ARGUMENTS;


	if(is_starcos_v3_4(card) || is_starcos_v3_2(card)){
		sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
			"not supported for STARCOS 3.4/3.2 cards");
		return SC_ERROR_NOT_SUPPORTED;
	}
	fid[0] = (file->id >> 8) & 0xff;
	fid[1] = file->id & 0xff;
	sc_format_apdu(card,&apdu,SC_APDU_CASE_3_SHORT, 0xE0, 0x02, 0x00);
	apdu.cla |= 0x80;
	apdu.lc   = 2;
	apdu.datalen = 2;
	apdu.data = fid;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	return sc_check_sw(card, apdu.sw1, apdu.sw2);
}

/** starcos_create_file
 * \param card pointer to the sc_card structure
 * \param file pointer to a sc_file object
 * \return SC_SUCCESS or error code
 *
 * This function creates MF, DF or EF based on the supplied
 * information in the sc_file structure (using starcos_process_acl).
 */
static int starcos_create_file(sc_card_t *card, sc_file_t *file)
{
	int    r;
	sc_starcos_create_data data;

	if(is_starcos_v3_4(card) || is_starcos_v3_2(card)){
		sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
			"not supported for STARCOS 3.4/3.2 cards");
		return SC_ERROR_NOT_SUPPORTED;
	}
	SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_VERBOSE);

	if (file->type == SC_FILE_TYPE_DF) {
		if (file->id == 0x3f00) {
			/* CREATE MF */
			r = starcos_process_acl(card, file, &data);
			if (r != SC_SUCCESS)
				return r;
			return starcos_create_mf(card, &data);
		} else {
			/* CREATE DF */
			r = starcos_process_acl(card, file, &data);
			if (r != SC_SUCCESS)
				return r;
			return starcos_create_df(card, &data);
		}
	} else if (file->type == SC_FILE_TYPE_WORKING_EF) {
		/* CREATE EF */
		r = starcos_process_acl(card, file, &data);
		if (r != SC_SUCCESS)
			return r;
		return starcos_create_ef(card, &data);
	} else
		return SC_ERROR_INVALID_ARGUMENTS;
}

/** starcos_erase_card
 * internal function to restore the delivery state
 * \param card pointer to the sc_card object
 * \return SC_SUCCESS or error code
 *
 * This function deletes the MF (for 'test cards' only).
 */
static int starcos_erase_card(sc_card_t *card)
{	/* restore the delivery state */
	int r;
	u8  sbuf[2];
	sc_apdu_t apdu;

	sbuf[0] = 0x3f;
	sbuf[1] = 0x00;
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xE4, 0x00, 0x00);
	apdu.cla |= 0x80;
	apdu.lc   = 2;
	apdu.datalen = 2;
	apdu.data = sbuf;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	/* invalidate cache */
	card->cache.valid = 0;
	if (apdu.sw1 == 0x69 && apdu.sw2 == 0x85)
		/* no MF to delete, ignore error */
		return SC_SUCCESS;
	else return sc_check_sw(card, apdu.sw1, apdu.sw2);
}

#define STARCOS_WKEY_CSIZE	124

/** starcos_write_key
 * set key in isf
 * \param card pointer to the sc_card object
 * \param data pointer to a sc_starcos_wkey_data structure
 * \return SC_SUCCESS or error code
 *
 * This function installs a key header in the ISF (based on the
 * information supplied in the sc_starcos_wkey_data structure)
 * and set a supplied key (depending on the mode).
 */
static int starcos_write_key(sc_card_t *card, sc_starcos_wkey_data *data)
{
	int       r;
	u8        sbuf[SC_MAX_APDU_BUFFER_SIZE];
	const u8 *p;
	size_t    len = sizeof(sbuf), tlen, offset = 0;
	sc_apdu_t       apdu;
	if(is_starcos_v3_4(card) || is_starcos_v3_2(card)){
		sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
			"not supported for STARCOS 3.4/3.2 cards");
		return SC_ERROR_NOT_SUPPORTED;
	}
	if (data->mode == 0) {	/* mode == 0 => install */
		/* install key header */
		sbuf[0] = 0xc1;	/* key header tag    */
		sbuf[1]	= 0x0c;	/* key header length */
		memcpy(sbuf + 2, data->key_header, 12);
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xf4,
			       data->mode, 0x00);
		apdu.cla |= 0x80;
		apdu.lc   = 14;
		apdu.datalen = 14;
		apdu.data = sbuf;

		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
			return sc_check_sw(card, apdu.sw1, apdu.sw2);
		if (data->key == NULL)
			return SC_SUCCESS;
	}

	if (data->key == NULL)
		return SC_ERROR_INVALID_ARGUMENTS;

	p    = data->key;
	tlen = data->key_len;
	while (tlen != 0) {
		/* transmit the key in chunks of STARCOS_WKEY_CSIZE bytes */
		u8 clen = tlen < STARCOS_WKEY_CSIZE ? tlen : STARCOS_WKEY_CSIZE;
		sbuf[0] = 0xc2;
		sbuf[1] = 3 + clen;
		sbuf[2] = data->kid;
		sbuf[3] = (offset >> 8) & 0xff;
		sbuf[4] = offset & 0xff;
		memcpy(sbuf+5, p, clen);
		len     = 5 + clen;
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xf4,
			       data->mode, 0x00);
		apdu.cla    |= 0x80;
		apdu.lc      = len;
		apdu.datalen = len;
		apdu.data    = sbuf;

		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
			return sc_check_sw(card, apdu.sw1, apdu.sw2);
		offset += clen;
		p      += clen;
		tlen   -= clen;
	}
	return SC_SUCCESS;
}

/** starcos_gen_key
 * generate public key pair
 * \param card pointer to the sc_card object
 * \param data pointer to a sc_starcos_gen_key_data structure
 * \return SC_SUCCESS or error code
 *
 * This function generates a public key pair and stores the created
 * private key in the ISF (specified by the KID).
 */
static int starcos_gen_key(sc_card_t *card, sc_starcos_gen_key_data *data)
{
	int	r;
	size_t	i, len = data->key_length >> 3;
	sc_apdu_t apdu;
	u8 rbuf[SC_MAX_APDU_BUFFER_SIZE];
	u8 sbuf[2], *p, *q;
	if(is_starcos_v3_4(card) || is_starcos_v3_2(card)){
		sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
			"not supported for STARCOS 3.4/3.2 cards");
		return SC_ERROR_NOT_SUPPORTED;
	}
	/* generate key */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x46,  0x00,
			data->key_id);
	apdu.le      = 0;
	sbuf[0] = (u8)(data->key_length >> 8);
	sbuf[1] = (u8)(data->key_length);
	apdu.data    = sbuf;
	apdu.lc      = 2;
	apdu.datalen = 2;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
		return sc_check_sw(card, apdu.sw1, apdu.sw2);
	/* read public key via READ PUBLIC KEY */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0xf0,  0x9c, 0x00);
	sbuf[0]      = data->key_id;
	apdu.cla    |= 0x80;
	apdu.data    = sbuf;
	apdu.datalen = 1;
	apdu.lc      = 1;
	apdu.resp    = rbuf;
	apdu.resplen = sizeof(rbuf);
	apdu.le      = 256;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
	if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
		return sc_check_sw(card, apdu.sw1, apdu.sw2);

	data->modulus = malloc(len);
	if (!data->modulus)
		return SC_ERROR_OUT_OF_MEMORY;
	p = data->modulus;
	/* XXX use tags to find starting position of the modulus */
	q = &rbuf[18];
	/* LSB to MSB -> MSB to LSB */
	for (i = len; i != 0; i--)
		*p++ = q[i - 1];

	return SC_SUCCESS;
}

/** starcos_set_security_env
 * sets the security environment
 * \param card pointer to the sc_card object
 * \param env pointer to a sc_security_env object
 * \param se_num not used here
 * \return SC_SUCCESS on success or an error code
 *
 * This function sets the security environment (using the starcos spk 2.3
 * command MANAGE SECURITY ENVIRONMENT). In case a COMPUTE SIGNATURE
 * operation is requested , this function tries to detect whether
 * COMPUTE SIGNATURE or INTERNAL AUTHENTICATE must be used for signature
 * calculation.
 */
static int starcos_set_security_env(sc_card_t *card,
				    const sc_security_env_t *env,
				    int se_num)
{
	u8              *p, *pp;
	int             r, operation = env->operation;
	sc_apdu_t   	apdu;
	u8              sbuf[SC_MAX_APDU_BUFFER_SIZE];
	starcos_ex_data *ex_data = (starcos_ex_data *)card->drv_data;
	p     = sbuf;

	if (is_starcos_v3_4(card) || is_starcos_v3_2(card)) {
		//starcos_v_3_2 specific commands
		if(is_starcos_v3_2(card)){
			int temp = 0;
			ex_data->fix_digestInfo = env->algorithm_flags; //TODO: set 0 if pkcs1 flag ?
			switch (operation) {
			case SC_SEC_OPERATION_DECIPHER:
				sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0x41, 0xB8);
				ex_data->sec_ops = SC_SEC_OPERATION_DECIPHER;
				break;
			case SC_SEC_OPERATION_SIGN:
				//ex_data->sec_ops = SC_SEC_OPERATION_SIGN;
				//sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0x41, 0xB6);//see ref manual "compute digital signature"
				/*TODO: for now: use internal authenticate to perform this operation
				* not adequate setting found yet, no function to compare given, additional information missing
				* return SC_ERROR_NOT_SUPPORTED; */
				//break;
			case SC_SEC_OPERATION_AUTHENTICATE:
				sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0x41, 0xA4); //see ref manual "internal authenticate"
				ex_data->sec_ops = SC_SEC_OPERATION_AUTHENTICATE;
				break;
			default:
				return SC_ERROR_INVALID_ARGUMENTS;
			}
			//0x84 tag: for private key	(only this tag allowed for symmetric key,
			//0x83 for asymmetric)
			if (env->flags & SC_SEC_ENV_KEY_REF_ASYMMETRIC){
				*p++ = 0x83;
			}else{
				*p++ = 0x84;
			}
			*p++ = env->key_ref_len;
			memcpy(p, env->key_ref, env->key_ref_len);
			//move pointer pos, adapt last byte
			p += env->key_ref_len -1;
			*p -= 0x03; //FIXME DIRTY HACK (somehow works?)
			p++;
			temp = starcos_find_algorithm_flags_3_2(card, env, p);
			if(temp < 0)
				return SC_ERROR_NOT_SUPPORTED;
			p += temp;
		}else{ //starcos_v_3_4 commands
			if (operation != SC_SEC_OPERATION_SIGN) {
				/* we only support signatures for now */
				sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
					"not supported for STARCOS 3.4 cards");
				return SC_ERROR_NOT_SUPPORTED;
			}
			//check if prerequisites are met
			if (!(env->algorithm_flags & SC_ALGORITHM_RSA_PAD_PKCS1) ||
					!(env->flags & SC_SEC_ENV_KEY_REF_PRESENT) || env->key_ref_len != 1) {
				SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);
			}
			sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0x41, 0xB6);
			/* don't know what these mean but doesn't matter as card seems to take
			 * algorithm / cipher from PKCS#1 padding prefix  */
			*p++ = 0x84;
			*p++ = 0x01;
			*p++ = 0x84;
			*p++ = 0x89;
			*p++ = 0x02;
			*p++ = 0x13;
			*p++ = 0x23;

			if (env->algorithm_flags == SC_ALGORITHM_RSA_PAD_PKCS1) {
				// input data will be already padded
				ex_data->fix_digestInfo = 0;
			} else {
				ex_data->fix_digestInfo = env->algorithm_flags;
			}
			ex_data->sec_ops = SC_SEC_OPERATION_SIGN;
		}

		/*Complete APDU and send*/
		apdu.data    = sbuf;
		apdu.datalen = p - sbuf;
		apdu.lc      = p - sbuf;
		apdu.le      = 0;
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));
		return SC_SUCCESS;
	}
	//older starcos versions here
	/* copy key reference, if present */
	if (env->flags & SC_SEC_ENV_KEY_REF_PRESENT) {
		if (env->flags & SC_SEC_ENV_KEY_REF_ASYMMETRIC)
			*p++ = 0x83;
		else
			*p++ = 0x84;
		*p++ = env->key_ref_len;
		memcpy(p, env->key_ref, env->key_ref_len);
		p += env->key_ref_len;
	}
	pp = p;
	if (operation == SC_SEC_OPERATION_DECIPHER){
		if (env->algorithm_flags & SC_ALGORITHM_RSA_PAD_PKCS1) {
			*p++ = 0x80;
			*p++ = 0x01;
			*p++ = 0x02;
		} else
			return SC_ERROR_INVALID_ARGUMENTS;
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0x81,
		               0xb8);
		apdu.data    = sbuf;
		apdu.datalen = p - sbuf;
		apdu.lc      = p - sbuf;
		apdu.le      = 0;
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));
		return SC_SUCCESS;
	}
	/* try COMPUTE SIGNATURE */
	if (operation == SC_SEC_OPERATION_SIGN && (
	    env->algorithm_flags & SC_ALGORITHM_RSA_PAD_PKCS1 ||
	    env->algorithm_flags & SC_ALGORITHM_RSA_PAD_ISO9796)) {

		if (env->flags & SC_SEC_ENV_ALG_REF_PRESENT) {
			*p++ = 0x80;
			*p++ = 0x01;
			*p++ = env->algorithm_ref & 0xFF;
		} else if (env->flags & SC_SEC_ENV_ALG_PRESENT &&
		            env->algorithm == SC_ALGORITHM_RSA) {
			/* set the method to use based on the algorithm_flags */
			*p++ = 0x80;
			*p++ = 0x01;
			if (env->algorithm_flags & SC_ALGORITHM_RSA_PAD_PKCS1) {
				if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_SHA1)
					*p++ = 0x12;
				else if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_RIPEMD160)
					*p++ = 0x22;
				else if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_MD5)
					*p++ = 0x32;
				else {
					/* can't use COMPUTE SIGNATURE =>
					 * try INTERNAL AUTHENTICATE */
					p = pp;
					operation = SC_SEC_OPERATION_AUTHENTICATE;
					goto try_authenticate;
				}
			} else if (env->algorithm_flags & SC_ALGORITHM_RSA_PAD_ISO9796) {
				if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_SHA1)
					*p++ = 0x11;
				else if (env->algorithm_flags & SC_ALGORITHM_RSA_HASH_RIPEMD160)
					*p++ = 0x21;
				else
					return SC_ERROR_INVALID_ARGUMENTS;
			} else
				return SC_ERROR_INVALID_ARGUMENTS;
		}
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0x41, 0xb6);
		apdu.data    = sbuf;
		apdu.datalen = p - sbuf;
		apdu.lc      = p - sbuf;
		apdu.le      = 0;
		/* TODO:we don't know whether to use
		 * COMPUTE SIGNATURE or INTERNAL AUTHENTICATE */
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 == 0x90 && apdu.sw2 == 0x00) {
			ex_data->fix_digestInfo = 0;
			ex_data->sec_ops        = SC_SEC_OPERATION_SIGN;
			return SC_SUCCESS;
		}
		/* reset pointer */
		p = pp;
		/* doesn't work => try next op */
		operation = SC_SEC_OPERATION_AUTHENTICATE;
	}
try_authenticate:
	/* try INTERNAL AUTHENTICATE */
	if (operation == SC_SEC_OPERATION_AUTHENTICATE &&
	    env->algorithm_flags & SC_ALGORITHM_RSA_PAD_PKCS1) {
		*p++ = 0x80;
		*p++ = 0x01;
		*p++ = 0x01;
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0x41,
		               0xa4);
		apdu.data    = sbuf;
		apdu.datalen = p - sbuf;
		apdu.lc      = p - sbuf;
		apdu.le      = 0;
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));
		ex_data->fix_digestInfo = env->algorithm_flags;
		ex_data->sec_ops        = SC_SEC_OPERATION_AUTHENTICATE;
		return SC_SUCCESS;
	}
	return SC_ERROR_INVALID_ARGUMENTS;
}

static int starcos_compute_signature(sc_card_t *card,
				     const u8 * data, size_t datalen,
				     u8 * out, size_t outlen)
{
	int r;
	sc_apdu_t apdu;
	u8 rbuf[SC_MAX_APDU_BUFFER_SIZE];
	u8 sbuf[SC_MAX_APDU_BUFFER_SIZE];
	starcos_ex_data *ex_data = (starcos_ex_data *)card->drv_data;

	if (datalen > SC_MAX_APDU_BUFFER_SIZE)
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);

	if (ex_data->sec_ops == SC_SEC_OPERATION_SIGN) {
		/*Create APDUs depending on types*/
		if (is_starcos_v3_4(card)) {
			size_t tmp_len;
			sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0x2A,
					   0x9E, 0x9A);
			apdu.resp = rbuf;
			apdu.resplen = sizeof(rbuf);
			apdu.le = 0;
			if (ex_data->fix_digestInfo) {
				// need to pad data
				unsigned int flags = ex_data->fix_digestInfo & SC_ALGORITHM_RSA_HASHES;
				if (flags == 0x00) {
					flags = SC_ALGORITHM_RSA_HASH_NONE;
				}
				tmp_len = sizeof(sbuf);
				r = sc_pkcs1_encode(card->ctx, flags, data, datalen, sbuf, &tmp_len, sizeof(sbuf));
				SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "sc_pkcs1_encode failed");
			} else {
				memcpy(sbuf, data, datalen);
				tmp_len = datalen;
			}

			apdu.data = sbuf;
			apdu.datalen = tmp_len;
			apdu.lc = tmp_len;
			apdu.resp = rbuf;
			apdu.resplen = sizeof(rbuf);
			apdu.le = 0;
		} else if (is_starcos_v3_2(card)) {
			//TODO: for now: not supported (Internal authenticate used instead)
			return SC_ERROR_NOT_SUPPORTED;
#if 0
			/*Following code for starcos 3.2. not working*/
			/*Set hash first*/
			sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x2A, 0x90, 0xA0);
			sbuf[0] = 0x90; // == hash tag
			sbuf[1] = datalen; // == hash len
			memcpy(&sbuf[2], data, datalen); //hash value
			apdu.data = sbuf;
			apdu.lc = datalen + 2;
			apdu.datalen = datalen + 2;
			apdu.resp = 0;
			apdu.resplen = 0;
			apdu.le = 0;
			/*Send first APDU*/
			r = sc_transmit_apdu(card, &apdu);
			SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
			if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
				SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE,
						   sc_check_sw(card, apdu.sw1, apdu.sw2));
			/*Second APDU: Sign command*/
			sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0x2A,
					   0X9E, 0x9A);
			apdu.resp = rbuf;
			apdu.resplen = sizeof(rbuf);
			apdu.le = 256;
#endif
		} else { /* Older starcos versions*/
			/* Set hash value first*/
			sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x2A, 0x90, 0x81);
			apdu.resp = rbuf;
			apdu.resplen = sizeof(rbuf);
			apdu.le = 0;
			memcpy(sbuf, data, datalen);
			apdu.data = sbuf;
			apdu.lc = datalen;
			apdu.datalen = datalen;
			/*Send first APDU*/
			r = sc_transmit_apdu(card, &apdu);
			SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
			if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
				SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE,
						   sc_check_sw(card, apdu.sw1, apdu.sw2));

			/*Second APDU: call COMPUTE SIGNATURE */
			sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0x2A,
					   0x9E, 0x9A);
			apdu.resp = rbuf;
			apdu.resplen = sizeof(rbuf);
			apdu.le = 256;

			apdu.lc = 0;
			apdu.datalen = 0;
		}
		/*Send APDU and process answer*/
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 == 0x90 && apdu.sw2 == 0x00) {
			size_t len = apdu.resplen > outlen ? outlen : apdu.resplen;
			memcpy(out, apdu.resp, len);
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, len);
		}
	} else if (ex_data->sec_ops == SC_SEC_OPERATION_AUTHENTICATE) {
		size_t tmp_len;
		/* call INTERNAL AUTHENTICATE */
		if (is_starcos_v3_4(card)) {
			return SC_ERROR_NOT_SUPPORTED;
		}
		if (is_starcos_v3_2(card)) {
			sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0x88, 0x00, 0x00);
			apdu.le = 0x00;
		} else {
			sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0x88, 0x10, 0x00);
		}
		/* fix/create DigestInfo structure (if necessary) */
		if (ex_data->fix_digestInfo) {
			unsigned int flags = ex_data->fix_digestInfo & SC_ALGORITHM_RSA_HASHES;
			if (flags == 0x0)
				/* XXX: assume no hash is wanted */
				flags = SC_ALGORITHM_RSA_HASH_NONE;
			tmp_len = sizeof(sbuf);
			r = sc_pkcs1_encode(card->ctx, flags, data, datalen,
					sbuf, &tmp_len, sizeof(sbuf));
			if (r < 0)
				return r;
		} else {
			memcpy(sbuf, data, datalen);
			tmp_len = datalen;
		}
		apdu.lc = tmp_len;
		apdu.data = sbuf;
		apdu.datalen = tmp_len;
		apdu.resp = rbuf;
		apdu.resplen = sizeof(rbuf);
		apdu.le = 256;

		/*Send APDU*/
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 == 0x90 && apdu.sw2 == 0x00) {
			size_t len = apdu.resplen > outlen ? outlen : apdu.resplen;

			memcpy(out, apdu.resp, len);
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, len);
		}
	} else {
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);
	}

	/* clear old state */
	ex_data->sec_ops = 0;
	ex_data->fix_digestInfo = 0;

	SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));
}

/*Sends APDU with command and data and returns received (decrypted) answer if successful. */
static int starcos_decipher(struct sc_card *card, const u8 * crgram, size_t crgram_len, u8 * out, size_t outlen){
	int r;
	struct sc_apdu apdu;
	u8 *sbuf = NULL;
	if (card == NULL || crgram == NULL || out == NULL) {
		return SC_ERROR_INVALID_ARGUMENTS;
	}
	LOG_FUNC_CALLED(card->ctx);
	sc_log(card->ctx,
	       "Card-starcos decipher: in-len %"SC_FORMAT_LEN_SIZE_T"u, out-len %"SC_FORMAT_LEN_SIZE_T"u",
	       crgram_len, outlen);

	/* INS: 0x2A  PERFORM SECURITY OPERATION
	 * P1:  0x80  Resp: Plain value
	 * P2:  0x86  Cmd: Padding indicator byte followed by cryptogram */

	/*starcos 3.2 part with extended size APDU*/
	if (is_starcos_v3_2(card)) {
		u8 *tempbuf = NULL;
		//just temporarily enable extended apdu, reset caps after operation finished: reset before every return
		unsigned long caps_buf = card->caps;
		caps_buf |= SC_CARD_CAP_APDU_EXT; //FIXME: should be set in starcos_init but that fct is buggy for 3.2
		if (caps_buf & SC_CARD_CAP_APDU_EXT) {
			sc_log(card->ctx, "Card-starcos decipher: Extended length needed");
		} else { //card-starcos_3-2 does not support multiple messages for one key, no way to send the data
			return SC_ERROR_NOT_SUPPORTED;
		}
		if (crgram_len + 1 > 255) //max of 255 bytes can be sent in short apdu, 1 byte added for padding indication
			sc_format_apdu(card, &apdu, SC_APDU_CASE_4_EXT, 0x2A, 0x80, 0x86);
		else
			sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0x2A, 0x80, 0x86);
		tempbuf = malloc(SC_MAX_APDU_BUFFER_SIZE);
		if (tempbuf == NULL) {
			return SC_ERROR_OUT_OF_MEMORY;
		}
		apdu.resp    = tempbuf;
		apdu.resplen = SC_MAX_APDU_BUFFER_SIZE;
		apdu.le      = 0x00;// starcos 3.2 expects 0x00 otherwise error -> buffer must be max size

		//extended size length is handled by apdu_send command -> content and one additional byte for padding
		sbuf = malloc(crgram_len + 1);
		if (sbuf == NULL){
			return SC_ERROR_OUT_OF_MEMORY;
		}
		apdu.data = sbuf;
		apdu.lc = crgram_len + 1;
		apdu.datalen = crgram_len + 1;
		/*0x02 == SC_ALGORITHM_RSA_PAD_PKCS1 pkcs1-padding. Not sure about 0x81 ... */
		sbuf[0] = (((starcos_ex_data*) card->drv_data)->sec_ops & SC_ALGORITHM_RSA_PAD_PKCS1) ? 0x02 : 0x81;
		memcpy(sbuf + 1, crgram, crgram_len);

		/*Send APDU and process answer*/
		r = sc_transmit_apdu(card, &apdu);
		//copy data from tempbuffer to out buff
		memcpy(out, tempbuf, (SC_MAX_APDU_BUFFER_SIZE > outlen) ? outlen : SC_MAX_APDU_BUFFER_SIZE);
		free(tempbuf);
		sc_mem_clear(sbuf, crgram_len + 1);
	} else { //older versions
		//normal sized APDU sufficient
		sc_format_apdu(card, &apdu, SC_APDU_CASE_4, 0x2A, 0x80, 0x86);
		apdu.resp    = out;
		apdu.resplen = outlen;
		apdu.le      = 0x00;// outlen;

		sbuf = malloc(crgram_len + 1);
		if (sbuf == NULL)
			return SC_ERROR_OUT_OF_MEMORY;
		sbuf[0] = 0x00 | (card->flags & SC_ALGORITHM_RSA_PAD_PKCS1);/* padding indicator byte, 0x00 = No further indication */
		memcpy(sbuf + 1, crgram, crgram_len);
		apdu.data = sbuf;
		apdu.lc = crgram_len + 1;
		apdu.datalen = crgram_len + 1;

		if(apdu.lc > sc_get_max_send_size(card)){ //for v3.2 already considered earlier
			/* Taken from iso7816*/
			apdu.flags |= SC_APDU_FLAGS_CHAINING;
		}
		if(apdu.le > sc_get_max_recv_size(card)){
			/*Taken from iso7816*/
			apdu.le = sc_get_max_recv_size(card);
		}
		//send and clear
		r = sc_transmit_apdu(card, &apdu);
		sc_mem_clear(sbuf, crgram_len + 1);
	}
	//return status
	free(sbuf);
	LOG_TEST_RET(card->ctx, r, "APDU transmit failed");
	if (apdu.sw1 == 0x90 && apdu.sw2 == 0x00)
		LOG_FUNC_RETURN(card->ctx, apdu.resplen);
	else
		LOG_FUNC_RETURN(card->ctx, sc_check_sw(card, apdu.sw1, apdu.sw2));
}

static int starcos_check_sw(sc_card_t *card, unsigned int sw1, unsigned int sw2)
{
	const int err_count = sizeof(starcos_errors)/sizeof(starcos_errors[0]);
	int i;
	sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL,
		"sw1 = 0x%02x, sw2 = 0x%02x\n", sw1, sw2);

	if (sw1 == 0x90)
		return SC_SUCCESS;
	/* check starcos error messages */
	for (i = 0; i < err_count; i++)
		if (starcos_errors[i].SWs == ((sw1 << 8) | sw2))
		{
			sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL, "%s\n", starcos_errors[i].errorstr);
			return starcos_errors[i].errorno;
		}
	/* iso error look-up*/
	return iso_ops->check_sw(card, sw1, sw2);
}

static int starcos_get_serialnr(sc_card_t *card, sc_serial_number_t *serial)
{
	int r;
	u8  rbuf[SC_MAX_APDU_BUFFER_SIZE];
	sc_apdu_t apdu;
	const u8 *tag = NULL;

	if (!serial)
		return SC_ERROR_INVALID_ARGUMENTS;
	/* see if we have cached serial number */
	if (card->serialnr.len) {
		memcpy(serial, &card->serialnr, sizeof(*serial));
		return SC_SUCCESS;
	}

	/*Search for serial number*/
	if (is_starcos_v3_4(card)) {
		return SC_ERROR_NOT_SUPPORTED;
	}
	if (is_starcos_v3_2(card)) {
		size_t taglen = 0;
		int  offs;

		sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0xCA, 0xDF, 0x20);
		apdu.cla = 0x00;
		apdu.resp = rbuf;
		apdu.resplen = sizeof(rbuf);
		apdu.le = 256;
		apdu.lc   = 0;
		apdu.datalen = 0;

		/* Send APDU */
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
			return SC_ERROR_INTERNAL;

		//Serial nr contained in data obj with following tag
		tag = sc_asn1_find_tag(card->ctx, apdu.resp, apdu.resplen, 0x9F6A, &taglen);
		if (tag  == NULL) {
			return SC_ERROR_INTERNAL;
		}
		//only consider last 8 bytes (apparently common practice)
		offs = taglen > 8 ? taglen - 8 : 0;
		memcpy(&card->serialnr, tag + offs, MIN(8, taglen));
		card->serialnr.len = 8;
	} else {
		//older starcos versions
		sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0xf6, 0x00, 0x00);
		apdu.cla |= 0x80;
		apdu.resp = rbuf;
		apdu.resplen = sizeof(rbuf);
		apdu.le   = 256;
		apdu.lc   = 0;
		apdu.datalen = 0;

		/*Send APDU*/
		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU transmit failed");
		if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
			return SC_ERROR_INTERNAL;

		/* cache serial number */
		memcpy(card->serialnr.value, apdu.resp, MIN(apdu.resplen, SC_MAX_SERIALNR));
		card->serialnr.len = MIN(apdu.resplen, SC_MAX_SERIALNR);
	}
	/* copy and return serial number */
	memcpy(serial, &card->serialnr, sizeof(*serial));

	return SC_SUCCESS;
}

static int starcos_card_ctl(sc_card_t *card, unsigned long cmd, void *ptr)
{
	sc_starcos_create_data *tmp;

	switch (cmd)
	{
	case SC_CARDCTL_STARCOS_CREATE_FILE:
		tmp = (sc_starcos_create_data *) ptr;
		if (tmp->type == SC_STARCOS_MF_DATA)
			return starcos_create_mf(card, tmp);
		else if (tmp->type == SC_STARCOS_DF_DATA)
			return starcos_create_df(card, tmp);
		else if (tmp->type == SC_STARCOS_EF_DATA)
			return starcos_create_ef(card, tmp);
		else
			return SC_ERROR_INTERNAL;
	case SC_CARDCTL_STARCOS_CREATE_END:
		return starcos_create_end(card, (sc_file_t *)ptr);
	case SC_CARDCTL_STARCOS_WRITE_KEY:
		return starcos_write_key(card, (sc_starcos_wkey_data *)ptr);
	case SC_CARDCTL_STARCOS_GENERATE_KEY:
		return starcos_gen_key(card, (sc_starcos_gen_key_data *)ptr);
	case SC_CARDCTL_ERASE_CARD:
		return starcos_erase_card(card);
	case SC_CARDCTL_GET_SERIALNR:
		return starcos_get_serialnr(card, (sc_serial_number_t *)ptr);
	default:
		return SC_ERROR_NOT_SUPPORTED;
	}
}

static int starcos_logout(sc_card_t *card)
{
	int r;
	sc_apdu_t apdu;
	const u8 mf_buf[2] = {0x3f, 0x00};

	sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL, "logout called\n");
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xA4, 0x00, 0x0C);
	apdu.le = 0;
	apdu.lc = 2;
	apdu.data    = mf_buf;
	apdu.datalen = 2;
	apdu.resplen = 0;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, SC_LOG_DEBUG_NORMAL, r, "APDU re-transmit failed");

	if (apdu.sw1 == 0x69 && apdu.sw2 == 0x85)
		/* the only possible reason for this error here is, afaik,
		 * that no MF exists, but then there's no need to logout
		 * => return SC_SUCCESS
		 */
		return SC_SUCCESS;
	return sc_check_sw(card, apdu.sw1, apdu.sw2);
}



/* As far as I understood, this funcion provides the settings how the in *data stored pin must be transmitted to the respective starcos card type.
*/
static int starcos_pin_cmd(sc_card_t *card, struct sc_pin_cmd_data *data,
			    int *tries_left)
{
	int ret = SC_SUCCESS;
	sc_debug(card->ctx, SC_LOG_DEBUG_NORMAL, "starcos_pin_cmd called\n");
	if (is_starcos_v3_4(card)) {
		SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_NORMAL);
		data->flags |= SC_PIN_CMD_NEED_PADDING;
		data->pin1.encoding = SC_PIN_ENCODING_GLP;
		ret = iso_ops->pin_cmd(card, data, tries_left);
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, ret);
	} else if (is_starcos_v3_2(card)) {
		SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_NORMAL);
		data->flags |= SC_PIN_CMD_NEED_PADDING;
		data->pin1.encoding = SC_PIN_ENCODING_ASCII;
		ret = iso_ops->pin_cmd(card, data, tries_left);
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, ret);
	} else {
		return SC_ERROR_NOT_SUPPORTED;
	}
}

static struct sc_card_driver * sc_get_driver(void)
{
	struct sc_card_driver *iso_drv = sc_get_iso7816_driver();
	if (iso_ops == NULL)
		iso_ops = iso_drv->ops;

	starcos_ops = *iso_drv->ops;
	starcos_ops.match_card = starcos_match_card;
	starcos_ops.init   = starcos_init;
	starcos_ops.finish = starcos_finish;
	starcos_ops.select_file = starcos_select_file;
	starcos_ops.check_sw    = starcos_check_sw;
	starcos_ops.create_file = starcos_create_file;
	starcos_ops.delete_file = NULL;
	starcos_ops.set_security_env  = starcos_set_security_env;
	starcos_ops.compute_signature = starcos_compute_signature;
	starcos_ops.card_ctl    = starcos_card_ctl;
	starcos_ops.logout      = starcos_logout;
	starcos_ops.pin_cmd     = starcos_pin_cmd;
	starcos_ops.decipher    = starcos_decipher;
	return &starcos_drv;
}

struct sc_card_driver * sc_get_starcos_driver(void)
{
	return sc_get_driver();
}
