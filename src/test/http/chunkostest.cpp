/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013  LiteSpeed Technologies, Inc.                        *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#ifdef RUN_TEST

#include "chunkostest.h"
#include <http/chunkoutputstream.h>
#include <edio/outputstream.h>
#include <util/iovec.h>

#include <string>
#include <stdio.h>
#include <sys/uio.h>
#include "test/unittest-cpp/UnitTest++/src/UnitTest++.h"


class TestOS : public OutputStream
{
    std::string     m_buf;
    int             m_count;
public:
    int write( const char * pBuf, int size )
    {
        if ( m_buf.size() < 8208 )
        {
            m_buf.append( pBuf, size );
            return size;
        }
        else
            return 0;
    }
    int writev( IOVec& iov, int total )
    {
        return OutputStream::writev( iov );        
    }
   
    void clearCache()
    {   m_buf.erase( m_buf.begin(), m_buf.end() );  }
    int flush()
    {   return 0;}
    int close()
    {   clearCache(); return 0;  }
    const std::string& getBuf() const { return m_buf; }
};

class TestOS1 : public OutputStream
{
    std::string     m_buf;
    int             m_count;
public:
    int write( const char * pBuf, int size )
    {
        m_count = !m_count;
        if ( m_count )
        {
            m_buf.append( pBuf, 1 );
            return 1;
        }
        else
            return 0;
    }
    int writev( IOVec& iov, int total )
    {
        m_count = !m_count;
        if ( m_count )
        {
            m_buf.append( (char *)(iov.begin()->iov_base), 1 );
            return 1;
        }
        else
            return 0;

    }

    void clearCache()
    {   m_buf.erase( m_buf.begin(), m_buf.end() );  }
    int flush()
    {   return 0;}
    int close()
    {   clearCache(); return 0;  }
    const std::string& getBuf() const { return m_buf; }
};

static void testBasic()
{
    const char * pResult = "b\r\nHello World\r\n0\r\n\r\n";
    TestOS           testOS;
    IOVec iov;
    int ht = 0;
    ChunkOutputStream chunkOS;
    chunkOS.setStream( &testOS, &iov );
    chunkOS.open();
    chunkOS.setBuffering( 1 );
    chunkOS.write( iov, ht, "Hello World", 11 );
    int len = testOS.getBuf().size();
    CHECK( 0 == len );
    chunkOS.close();
    len = testOS.getBuf().size();
    CHECK( 21 == len );
    CHECK( strcmp( pResult , testOS.getBuf().c_str() ) == 0 );
    testOS.clearCache();
    CHECK( 0 == testOS.getBuf().size() );
    chunkOS.open();
    chunkOS.setBuffering( 1 );
    chunkOS.write( iov, ht, "H", 1 );
    chunkOS.write( iov, ht, "e", 1 );
    chunkOS.write( iov, ht, "ll", 2 );
    chunkOS.write( iov, ht, "o W", 3 );
    chunkOS.write( iov, ht, "orld", 4 );
    len = testOS.getBuf().size();
    CHECK( 0 == len );
    chunkOS.close();
    len = testOS.getBuf().size();
    CHECK( 21 == len );
    CHECK( strcmp( pResult , testOS.getBuf().c_str() ) == 0 );
}

static int chunkLen( int num )
{
    int len = num + 4;
    do
    {
        num >>= 4;
        ++len;
    }while( num > 0 );
    return len;
}

void testChunkBuffer()
{
    TestOS           testOS;
    ChunkOutputStream chunkOS;
    IOVec iov;
    int ht = 0;
    chunkOS.setStream( &testOS, &iov );
    chunkOS.open();
    chunkOS.setBuffering( 1 );
    int len, ret, i;
    for( i = 1; i < 8192; i++ )
    {
        chunkOS.write( iov, ht, "a", 1 );
        len = testOS.getBuf().size();
        if ( len != 0 )
            break;
    }
    //printf( "i = %d\n", i );
    CHECK( CHUNK_BUFSIZE == i );
    CHECK( chunkLen( CHUNK_BUFSIZE ) == len );
    testOS.clearCache();
    char achBuf[20480];
    memset( achBuf, 'b', 20480 );

    chunkOS.write( iov, ht, achBuf, CHUNK_BUFSIZE );
    len = testOS.getBuf().size();
    CHECK( chunkLen( CHUNK_BUFSIZE ) == len );
    testOS.clearCache();

    ret = chunkOS.write( iov, ht, achBuf, CHUNK_BUFSIZE - 1 );
    CHECK( CHUNK_BUFSIZE - 1 == ret );
    len = testOS.getBuf().size();
    //printf( "len = %d, expect %d", len, chunkLen( MAX_CHUNK_SIZE - 1) );
    CHECK( 0 == len );
    chunkOS.write( iov, ht, achBuf, 1 );
    len = testOS.getBuf().size();
    CHECK( chunkLen( CHUNK_BUFSIZE ) == len );
    testOS.clearCache();

    ret = chunkOS.write( iov, ht, achBuf, MAX_CHUNK_SIZE );
    CHECK( MAX_CHUNK_SIZE == ret );
    len = testOS.getBuf().size();
    CHECK( chunkLen( MAX_CHUNK_SIZE )== len );
    testOS.clearCache();

    ret = chunkOS.write( iov, ht, achBuf, MAX_CHUNK_SIZE * 3 );
    //printf( "ret = %d\n", ret );
    CHECK( MAX_CHUNK_SIZE * 2 == ret );
    len = testOS.getBuf().size();
    CHECK( 2 * ( chunkLen( MAX_CHUNK_SIZE ) ) == len );
    testOS.clearCache();
    CHECK( chunkOS.write( iov, ht, (char *)achBuf + MAX_CHUNK_SIZE * 2,
                    MAX_CHUNK_SIZE) == MAX_CHUNK_SIZE );
    len = testOS.getBuf().size();
    CHECK( ( chunkLen( MAX_CHUNK_SIZE ) ) == len );
    testOS.clearCache();
    
    ret = chunkOS.write( iov, ht, achBuf, MAX_CHUNK_SIZE + 1 );
    CHECK( MAX_CHUNK_SIZE + 1 == ret );
    len = testOS.getBuf().size();
    CHECK( chunkLen( MAX_CHUNK_SIZE ) == len );
    testOS.clearCache();
    chunkOS.close();
    len = testOS.getBuf().size();
    CHECK( chunkLen( 1 ) + 5 == len );
    testOS.clearCache();
    
    //chunkOS.write( achBuf, 1023 );

}

void testWithHeader()
{
    const char * header[] =
    {
        "HTTP/1.1 200 OK\r\n",
        "Content-Type: text/html\r\n",
        "Transfer-Encoding: chunked\r\n",
        "\r\n"
    };
    TestOS1           testOS;
    TestOS            test2;
    ChunkOutputStream chunkOS;
    IOVec iov;
    int hs = 0;
    int ht;
    int ret;
    int size;
    unsigned int i;
    const char * pBody = "Hello World";
    for( i = 0 ; i < sizeof( header ) / sizeof( char *); ++i )
    {
        iov.append( header[i], strlen( header[i] ) );
        hs += strlen( header[i] );
    }
    ht = hs;
    chunkOS.setStream( &test2, &iov );
    chunkOS.open();
    ret = chunkOS.write( iov, ht, pBody, 11 );
    CHECK( ret == 11 );
    ret = chunkOS.close(iov, ht);
    CHECK( ret == 0 );
    size = test2.getBuf().size();
    CHECK( size == hs + chunkLen( 11 ) + 5 );
    CHECK( memcmp( test2.getBuf().c_str() + hs + chunkLen( 11 ) - 11 - 2,
                    pBody, 11 ) == 0 );
    
        
    hs = 0;
    for( i = 0 ; i < sizeof( header ) / sizeof( char *); ++i )
    {
        iov.append( header[i], strlen( header[i] ) );
        hs += strlen( header[i] );
    }
    ht = hs;
    char achBuf[MAX_CHUNK_SIZE * 2];
    memset( achBuf, 'c', MAX_CHUNK_SIZE );
    memset( (char *)achBuf + MAX_CHUNK_SIZE, 'd', MAX_CHUNK_SIZE );
    chunkOS.setStream( &testOS, &iov );
    chunkOS.open();
    while( ht > 0 )
    {
        ret = chunkOS.write( iov, ht, achBuf, MAX_CHUNK_SIZE );
        CHECK( ret == 0 );
    }
    CHECK( iov.len() == 0 );
    const char * pBuf = testOS.getBuf().c_str();
    for( i = 0 ; i < sizeof( header ) / sizeof( char *); ++i )
    {
        CHECK( strncmp( pBuf, header[i], strlen( header[i] )) == 0 );
        pBuf += strlen( header[i] );
    }
    
    while( ret == 0 )
    {
        ret = chunkOS.write( iov, ht, achBuf, MAX_CHUNK_SIZE );
    }
    CHECK( ret == MAX_CHUNK_SIZE );
    pBuf = testOS.getBuf().c_str();
    CHECK( memcmp( pBuf + hs +
            chunkLen( MAX_CHUNK_SIZE ) - MAX_CHUNK_SIZE - 2,
            achBuf, MAX_CHUNK_SIZE ) == 0 );
    ret = 0;
    while( ret == 0 )
    {
        ret = chunkOS.write( iov, ht, (char *)achBuf + MAX_CHUNK_SIZE, MAX_CHUNK_SIZE );
    }
    CHECK( ret == MAX_CHUNK_SIZE );
    pBuf = testOS.getBuf().c_str() + hs +
        chunkLen( MAX_CHUNK_SIZE ) * 2 - MAX_CHUNK_SIZE - 2;
    CHECK( memcmp( pBuf, 
        (char *)achBuf + MAX_CHUNK_SIZE, MAX_CHUNK_SIZE ) == 0 );
    ret = chunkOS.close(iov, ht);
    CHECK( ret == 1 );
    while( ret == 1 )
    {
        ret = chunkOS.flush();
    }
    CHECK( ret == 0 );
    size = testOS.getBuf().size();
    CHECK( size == hs + chunkLen( MAX_CHUNK_SIZE ) * 2 + 5 );
    CHECK( memcmp(
            testOS.getBuf().c_str() + hs + chunkLen( MAX_CHUNK_SIZE ) * 2,
            "0\r\n\r\n", 5 ) == 0 );
    
}

TEST(ChunkOSTest__)
{
    testBasic();
    testChunkBuffer();
    testWithHeader();
}

#endif