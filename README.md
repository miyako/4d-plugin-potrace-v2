![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-potrace-v2)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-potrace-v2/total)

# 4d-plugin-potrace-v2
Rewrite of [Potrace](http://potrace.sourceforge.net) plugin; project based sample DB and ``PROCESS 4D TAGS`` scaffolding.

### Syntax

[miyako.github.io](https://miyako.github.io/2019/06/24/4d-plugin-potrace-v2.html)

#### Discussion

to compile with Windows Visual Studio

```c
#ifndef _UINT64_T
#define _UINT64_T
typedef unsigned long long uint64_t;
#endif /* _UINT64_T */
```

```c
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
```
