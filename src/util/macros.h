#ifndef OLX_UTILS_MACROS_H
#define OLX_UTILS_MACROS_H

#ifdef _MSC_VER
// MSVC has no __typeof__ keyword; map it onto C++11 decltype.
// (GCC/Clang provide __typeof__ as a builtin.)
#define __typeof__(x) decltype(x)
#endif

//NOTE: It's important that these are defined on a single-line
//since all references to __LINE__ must evaluate to the same value.

#define foreach( i, c )\
  typedef __typeof__( c ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__)& _LINENAME(C_, __LINE__) = (c); for( __typeof__(_LINENAME(C_, __LINE__).begin()) i = _LINENAME(C_, __LINE__).begin(); i != _LINENAME(C_, __LINE__).end(); ++i )
//  typedef __typeof__( c ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__)& _LINENAME(C_, __LINE__) = (c); for( _LINENAME(T_, __LINE__)::iterator i = _LINENAME(C_, __LINE__).begin(); i != _LINENAME(C_, __LINE__).end(); ++i )

#define foreach_bool( i, c )\
  typedef __typeof__( c ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__)& _LINENAME(C_, __LINE__) = (c); for( __typeof__(_LINENAME(C_, __LINE__).begin()) i = _LINENAME(C_, __LINE__).begin(); i; ++i )
  
#define foreach_delete( i, c )\
  typedef __typeof__( c ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__)& _LINENAME(C_, __LINE__) = (c); for( __typeof__(_LINENAME(C_, __LINE__).begin()) i = _LINENAME(C_, __LINE__).begin(), next; (i != _LINENAME(C_, __LINE__).end()) && (next = i, ++next, true); i = next )
  
#ifdef _MSC_VER
#define const_foreach( i, c )\
  typedef __typeof__( c ) _LINENAME(T_, __LINE__); const _LINENAME(T_, __LINE__)& _LINENAME(C_, __LINE__) = (c); for( __typeof__(_LINENAME(C_, __LINE__).begin()) i = _LINENAME(C_, __LINE__).begin(); i != _LINENAME(C_, __LINE__).end(); ++i )
#else
#define const_foreach( i, c )\
  typedef __typeof__( c ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__)& _LINENAME(C_, __LINE__) = (c); for( __typeof__(_LINENAME(C_, __LINE__).begin()) i = _LINENAME(C_, __LINE__).begin(); i != _LINENAME(C_, __LINE__).end(); ++i )
#endif

#define reverse_foreach( i, c )\
  typedef __typeof__( c ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__)& _LINENAME(C_, __LINE__) = (c); for( __typeof__(_LINENAME(C_, __LINE__).rbegin()) i = _LINENAME(C_, __LINE__).rbegin(); i != _LINENAME(C_, __LINE__).rend(); ++i )
  
#define forrange( i, b, e )\
  typedef __typeof__( b ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__) _LINENAME(E_, __LINE__) = (e); for( _LINENAME(T_, __LINE__) i = (b); i != _LINENAME(E_, __LINE__); ++i )

#define forrange_delete( i, b, e )\
  typedef __typeof__( b ) _LINENAME(T_, __LINE__); _LINENAME(T_, __LINE__) _LINENAME(E_, __LINE__) = (e); for( _LINENAME(T_, __LINE__) i = (b), next; (i != _LINENAME(E_, __LINE__)) && (next = i, ++next, true); i = next )

#define forrange_bool( i, b )\
  typedef __typeof__( b ) _LINENAME(T_, __LINE__); for( _LINENAME(T_, __LINE__) i = (b); i; ++i )

#define let_(i, v) __typeof__(v) i = v

/* Note:
 We cannot use the std offsetof macro because it is not a POD.
 We do the hack +sizeof(type) ... -sizeof(type) to avoid warnings for accessing a NULL ptr.
 */
#define __OLX_OFFSETOF(type, member) ( (char*) ( & ( (type*)( (char*)0 + sizeof(type) ) )->member ) - sizeof(type) )
#define __OLX_BASETHIS(type, member) (type*) ( (char*)this - __OLX_OFFSETOF(type,member) )

#define _LINENAME_CAT( name, line ) name##line
#define _LINENAME( name, line ) _LINENAME_CAT( name, line )

#endif //OLX_UTILS_MACROS_H
