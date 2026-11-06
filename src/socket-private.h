
#if !defined(MEDUSA_SOCKET_PRIVATE_H)
#define MEDUSA_SOCKET_PRIVATE_H

#include <errno.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#endif

/*
 * winsock reports socket errors (WSAGetLastError, SO_ERROR) as WSAE* codes,
 * map them to posix errno so callers see a single error namespace. values
 * below WSABASEERR are already errno and are returned unchanged.
 */
static inline int medusa_errno_from_socket_error (int error)
{
#if defined(__WINDOWS__)
        if (error < WSABASEERR) {
                return error;
        }
        switch (error) {
                case WSAEINTR:                  return EINTR;
                case WSAEBADF:                  return EBADF;
                case WSAEACCES:                 return EACCES;
                case WSAEFAULT:                 return EFAULT;
                case WSAEINVAL:                 return EINVAL;
                case WSAEMFILE:                 return EMFILE;
                case WSAEWOULDBLOCK:            return EWOULDBLOCK;
                case WSATRY_AGAIN:              return EAGAIN;
                case WSAEINPROGRESS:            return EINPROGRESS;
                case WSAEALREADY:               return EALREADY;
                case WSAENOTSOCK:               return ENOTSOCK;
                case WSAEDESTADDRREQ:           return EDESTADDRREQ;
                case WSAEMSGSIZE:               return EMSGSIZE;
                case WSAEPROTOTYPE:             return EPROTOTYPE;
                case WSAENOPROTOOPT:            return ENOPROTOOPT;
                case WSAEPROTONOSUPPORT:        return EPROTONOSUPPORT;
                case WSAESOCKTNOSUPPORT:        return EPROTONOSUPPORT;
                case WSAEOPNOTSUPP:             return EOPNOTSUPP;
                case WSAEPFNOSUPPORT:           return EAFNOSUPPORT;
                case WSAEAFNOSUPPORT:           return EAFNOSUPPORT;
                case WSAEADDRINUSE:             return EADDRINUSE;
                case WSAEADDRNOTAVAIL:          return EADDRNOTAVAIL;
                case WSAENETDOWN:               return ENETDOWN;
                case WSAENETUNREACH:            return ENETUNREACH;
                case WSAENETRESET:              return ENETRESET;
                case WSAECONNABORTED:           return ECONNABORTED;
                case WSAECONNRESET:             return ECONNRESET;
                case WSAENOBUFS:                return ENOBUFS;
                case WSAEISCONN:                return EISCONN;
                case WSAENOTCONN:               return ENOTCONN;
                case WSAESHUTDOWN:              return ENOTCONN;
                case WSAETIMEDOUT:              return ETIMEDOUT;
                case WSAECONNREFUSED:           return ECONNREFUSED;
                case WSAELOOP:                  return ELOOP;
                case WSAENAMETOOLONG:           return ENAMETOOLONG;
                case WSAEHOSTDOWN:              return EHOSTUNREACH;
                case WSAEHOSTUNREACH:           return EHOSTUNREACH;
                case WSAENOTEMPTY:              return ENOTEMPTY;
                default:                        return EIO;
        }
#else
        return error;
#endif
}

#endif
