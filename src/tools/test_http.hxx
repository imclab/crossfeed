// test_http.hxx
#ifndef _TEST_HTTP_HXX_
#define _TEST_HTTP_HXX_

#ifdef _MSC_VER
    #define SWRITE(a,b,c) send(a,b,c,0)
    #define SREAD(a,b,c)  recv(a,b,c,0)
    #define SCLOSE closesocket
    #define SERROR(a) (a == SOCKET_ERROR)
    #define PERROR(a) win_wsa_perror(a)
#else
    #define SWRITE write
    #define SREAD  read
    #define SCLOSE close
    #define SERROR(a) (a < 0)
    #define PERROR(a) perror(a)
#endif

#endif // #ifndef _TEST_HTTP_HXX_
// eof - test_http.hxx


