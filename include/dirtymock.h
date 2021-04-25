#ifndef DIRTYMOCK_H
#define DIRTYMOCK_H

#ifdef TESTING
#define DIRTYMOCK(fn_name) real_##fn_name
#else
#define DIRTYMOCK(fn_name) fn_name
#endif

#endif // DIRTYMOCK_H
