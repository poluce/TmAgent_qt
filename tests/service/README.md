# Service Tests

当前包含：

- `MessageRouterTest`：验证 `@Agent` / `@all` / 默认路由 / unresolved mention 等基础路由规则。

运行方式（Windows + Qt）：

```powershell
cd tests/service
mkdir build; cd build
qmake ..
mingw32-make -j4
.\release\MessageRouterTest.exe
```
