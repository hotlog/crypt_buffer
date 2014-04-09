
/*
 * copyright: 2014
 * name : mhogo mchungu
 * email: mhogomchungu@gmail.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "crypt_buffer.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gcrypt.h>
#pragma GCC diagnostic warning "-Wdeprecated-declarations"

#define KEY_LENGTH 32
#define SALT_SIZE  16
#define IV_SIZE    16
#define LOAD_INFO_SIZE  32
#define MAGIC_STRING "TRUE"
#define MAGIC_STRING_LENGTH 4

#define PBKDF2_ITERATIONS 5000

/*
 * This library takes a block of data and returns an encrypted version of the data.
 * This library also has the ability to reverse the above action.
 *
 * A user gives a password of specified size and the library coverts it to a 32 byte key using
 * pbkdf2 with iteration count of 5000 and a hash function of sha2.
 *
 * Data is encrypted using CBC mode of 256bit AES.
 *
 * The format of the encrypted data:
 * First 16 bytes from offset 0 stores pbkdf2 salt.
 * Second 16 bytes from offset 16 stores AES initialization vector.
 * Third 4 bytes from offset 32 stores the size of the load given by the user.
 * Fourth 4 bytes from offset 36 stores a string "TRUE" that is used to verify encryption key during decryption.
 * 24 bytes from offset 40 are currently unsed.
 * The load the user gave to be stored encrypted starts from offset 64.The load will be padded up to a multiple of 32.
 *
 * Encrypted data starts at offset 32.
 *
 * Encrypted data will take a minimum of 64 bytes and a maximum of 64 + load size + 31 bytes
 */

struct crypt_buffer_ctx_1
{
	char * buffer ;
	size_t buffer_size ;
	gcry_cipher_hd_t h ;
} ;

static int _failed( gcry_error_t r )
{
	return r != GPG_ERR_NO_ERROR ;
}

static int _passed( gcry_error_t r )
{
	return r == GPG_ERR_NO_ERROR ;
}

int crypt_buffer_init( crypt_buffer_ctx * ctx )
{
	gcry_error_t r ;

	gcry_cipher_hd_t handle ;

	crypt_buffer_ctx ctx_1 ;

	if( gcry_control( GCRYCTL_INITIALIZATION_FINISHED_P ) != 0 ){
		gcry_check_version( NULL ) ;
		gcry_control( GCRYCTL_INITIALIZATION_FINISHED,0 ) ;
	}

	r = gcry_cipher_open( &handle,GCRY_CIPHER_AES256,GCRY_CIPHER_MODE_CBC,0 ) ;

	if( _failed( r ) ){
		*ctx = NULL ;
		return 0 ;
	}else{
		ctx_1 = malloc( sizeof( struct crypt_buffer_ctx_1 ) ) ;
		if( ctx_1 == NULL ){
			gcry_cipher_close( handle ) ;
			*ctx = NULL ;
			return 0 ;
		}else{
			ctx_1->buffer      = NULL ;
			ctx_1->buffer_size = 0 ;
			ctx_1->h           = handle ;
			*ctx               = ctx_1 ;
			return 1 ;
		}
	}
}

void crypt_buffer_uninit( crypt_buffer_ctx * ctx )
{
	crypt_buffer_ctx tx ;

	if( ctx != NULL && *ctx != NULL ){
		tx = *ctx ;
		*ctx = NULL ;
		gcry_cipher_close( tx->h ) ;
		free( tx->buffer ) ;
		free( tx ) ;
	}
}

static int  _get_random_data( char * buffer,size_t buffer_size )
{
	int fd = open( "/dev/urandom",O_RDONLY ) ;
	if( fd != -1 ){
		read( fd,buffer,buffer_size ) ;
		close( fd ) ;
		return 0 ;
	}else{
		return 1 ;
	}
}

static gcry_error_t _create_key( const char * salt,size_t salt_size,const char * input_key,
				 size_t input_key_length,char * output_key,size_t output_key_size )
{
	return gcry_kdf_derive( input_key,input_key_length,GCRY_KDF_PBKDF2,GCRY_MD_SHA256,
				salt,salt_size,PBKDF2_ITERATIONS,output_key_size,output_key ) ;
}

static int _set_gcrypt_handle( crypt_buffer_ctx ctx,const char * password,
			       size_t passphrase_size,const char * salt,size_t salt_size,const char * iv,size_t iv_size )
{
	char key[ KEY_LENGTH ] ;

	gcry_error_t r ;

	gcry_cipher_hd_t handle = ctx->h ;

	r = gcry_cipher_reset( handle ) ;

	if( _passed( r ) ){
		r = _create_key( salt,salt_size,password,passphrase_size,key,KEY_LENGTH ) ;
		if( _passed( r ) ){
			r = gcry_cipher_setkey( handle,key,KEY_LENGTH ) ;
			if( _passed( r ) ){
				r = gcry_cipher_setiv( handle,iv,iv_size ) ;
				if( _passed( r ) ){
					return 1 ;
				}
			}
		}
	}

	return 0 ;
}

static char * _expand_buffer( crypt_buffer_ctx h,size_t z )
{
	char * e = NULL ;

	if( h == NULL ){
		return 0 ;
	}
	
	if( h->buffer_size < z ){

		e = realloc( h->buffer,z ) ;

		if( e != NULL ){
			h->buffer = e ;
			h->buffer_size = z ;
			return e ;
		}else{
			return NULL ;
		}
	}else{
		return h->buffer ;
	}
}

int crypt_buffer_encrypt( crypt_buffer_ctx ctx,const void * buffer,u_int32_t buffer_size,
			  const void * password,size_t passphrase_size,crypt_buffer_result * r )
{
	char buff[ SALT_SIZE + IV_SIZE ] ;

	gcry_error_t z ;

	size_t len ;
	size_t k = buffer_size ;

	char * e ;
	const char * salt = buff ;
	const char * iv   = buff + SALT_SIZE ;

	if( _get_random_data( buff,SALT_SIZE + IV_SIZE ) ){
		return 0 ;
	}

	/*
	 * make sure the block buffer we are going to encrypt is a multiple of 32
	 */
	while( k % 32 != 0 ){
		k += 1 ;
	}

	e = _expand_buffer( ctx,k + SALT_SIZE + IV_SIZE + LOAD_INFO_SIZE ) ;

	if( e == NULL ){
		return 0 ;
	}

	if( _set_gcrypt_handle( ctx,password,passphrase_size,salt,SALT_SIZE,iv,IV_SIZE ) ){

		len = SALT_SIZE + IV_SIZE ;

		/*
		 * The first 32 bytes block of cipher text starts at offset 0.
		 * First 16 bytes is for pbkdf2 salt.
		 * Second 16 bytes is for AES initialization vector.
		 * These informations are stored in clear text but are indistinguishable from cipher text
		 */
		memcpy( e,buff,len ) ;
		/*
		 * The second 32 bytes block of cipher text starts at offset 32.
		 * The first 4 bytes at offset 32 stores the size of the clear text we are going to encrypt
		 */
		memcpy( e + len,&buffer_size,sizeof( u_int32_t ) ) ;
		/*
		 * The second 4 bytes at offset 36 stores "TRUE" bytes to be used to verify decryption key
		 * The remaining 24 bytes are currently unused.
		 */
		memcpy( e + len + sizeof( u_int32_t ),MAGIC_STRING,MAGIC_STRING_LENGTH ) ;
		/*
		 * The third block starts at offset 64 and it stores the data content we were asked to encrypt
		 */
		memcpy( e + len + LOAD_INFO_SIZE,buffer,buffer_size ) ;

		/*
		 * Encryption starts at offset 32
		 */
		z = gcry_cipher_encrypt( ctx->h,e + len,LOAD_INFO_SIZE + k,NULL,0 ) ;

		if( _passed( z ) ){
			r->buffer = ctx->buffer ;
			/*
			 * SALT_SIZE + IV_SIZE + LOAD_INFO_SIZE will equal 64.
			 * k will equal the size of the data we were asked to encrypt rounded up to a multiple of 32
			 */
			r->length = k + SALT_SIZE + IV_SIZE + LOAD_INFO_SIZE ;
			return 1 ;
		}else{
			return 0 ;
		}
	}else{
		return 0 ;
	}
}

/*
 * The password is assumed to be correct if the 4 bytes from offset 36 equal "TRUE"
 */
static int _password_is_correct( const char * buffer )
{
	return memcmp( buffer + sizeof( u_int32_t ),MAGIC_STRING,MAGIC_STRING_LENGTH ) == 0 ;
}

static u_int32_t _get_data_length( const char * buffer )
{
	u_int32_t l ;
	memcpy( &l,buffer,sizeof( u_int32_t ) ) ;
	return l ;
}

int crypt_buffer_decrypt( crypt_buffer_ctx ctx,const void * buffer,u_int32_t buffer_size,
			  const void * password,size_t passphrase_size,crypt_buffer_result * r )
{
	gcry_error_t z ;

	char * e ;

	const char * buff = buffer ;
	const char * salt = buff ;
	const char * iv   = buff + SALT_SIZE ;

	size_t len = buffer_size - ( SALT_SIZE + IV_SIZE ) ;

	e = _expand_buffer( ctx,buffer_size ) ;

	if( e == NULL ){
		return 0 ;
	}

	if( _set_gcrypt_handle( ctx,password,passphrase_size,salt,SALT_SIZE,iv,IV_SIZE ) ){

		/*
		 * Skip to offset 32 and start decryption from there.Thats because the first 32 bytes
		 * holds salt and IV and are stored unencrypted.
		 */
		z = gcry_cipher_decrypt( ctx->h,e,len,buff + SALT_SIZE + IV_SIZE,len ) ;

		if( _passed( z ) ){

			if( _password_is_correct( e ) ){

				r->buffer = e + LOAD_INFO_SIZE ;
				r->length = _get_data_length( e ) ;

				if( r->length <= buffer_size - ( SALT_SIZE + IV_SIZE + LOAD_INFO_SIZE ) ){
					/*
					 * make sure the stored size is within expected range
					 */
					return 1 ;
				}else{
					/*
					 * something funny is going on,the stored size of data is larger than data itself
					 */
					return 0 ;
				}
			}else{
				return 0 ;
			}
		}else{
			return 0 ;
		}
	}else{
		return 0 ;
	}
}
