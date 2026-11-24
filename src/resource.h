/*
 * src/resource.h -- NetBall 资源 ID（只放资源 ID，命令 ID / 窗口类名见 src/app_common.h）
 *
 * 注意：IDI_APP / IDR_MANIFEST 在 src/app_common.h 中已有同名同值定义。
 * 这里统一加 #ifndef 守卫，保证 resource.h 与 app_common.h 被同一个 .cpp 同时包含时
 * 不会产生宏重复定义。
 */

#ifndef NETBALL_RESOURCE_H
#define NETBALL_RESOURCE_H

#ifndef IDI_APP
#define IDI_APP        101
#endif

#ifndef IDR_MANIFEST
#define IDR_MANIFEST   1
#endif

#ifndef IDS_APPNAME
#define IDS_APPNAME    200
#endif

#ifndef IDS_VERSION
#define IDS_VERSION    201
#endif

#endif /* NETBALL_RESOURCE_H */
