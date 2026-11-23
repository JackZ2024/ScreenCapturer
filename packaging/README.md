# 发布 ScreenCapturer 更新

在根目录 `CMakeLists.txt` 更新版本号，例如 `1.5.1`，然后从同一版本的源码构建 Windows 和 macOS 发布包。将附件上传到 `JackZ2024/ScreenCapturer` 的 GitHub Release，标签使用 `v1.5.1`，按正式版发布并设为 Latest。两端的附件准备好后再发布。

旧版软件需要先手动升级到带更新功能的版本。可以用两个连续的版本验收，先运行前一个版本，再发布后一个版本。启动时关闭更新窗口后重新启动，应该不再弹窗。在「关于 → 检查更新」仍应看到后一个版本。点击「更新」后确认新版启动、关于页版本号变化，原有设置和钉图仍可读取。

## Windows

在已配置 Qt 和 MSVC 的 PowerShell 或 GitHub Actions 中构建 Release 版本。构建 `ScreenCapturer` 时也会构建 `ScreenCapturerUpdater`。打包脚本通过 `windeployqt` 收集 Qt 依赖，并为 ZIP 中的软件文件生成清单。在仓库根目录运行下列命令。

```powershell
.\packaging\package-windows.ps1 -Architecture x64 -BuildOutput app -OutputDirectory build/release
```

将生成的 `ScreenCapturer-windows-x64.zip` 上传为 Release 附件。ARM64 和 x86 构建分别使用 `arm64` 和 `x86`，并选用对应的 Qt 和应用程序二进制。多配置构建用 `-BuildOutput app/Release` 指向实际输出位置。客户端使用 GitHub 返回的附件大小和 SHA-256 校验下载。ZIP 根目录包含 `ScreenCapturer.exe`、`ScreenCapturerUpdater.exe`、`ScreenCapturer-files.txt`、DLL 和 Qt 插件。

首次使用时把 ZIP 完整解压到可写目录。目标系统需要 Windows 自带的 `tar.exe` 和对应架构的 Microsoft Visual C++ Redistributable。升级会在原解压目录替换软件文件并保留其他文件。若新版包含与用户文件同名的路径，更新会先提示冲突，旧版仍可继续运行。

如果便携版目录里已有更新助手，但提示缺少 `ScreenCapturer-files.txt`，请用打包脚本重新生成 ZIP。清单须同时存在于当前运行目录和将要下载的新版 ZIP 根目录。清单由实际打包的文件生成，只列程序随包分发的文件。

## macOS

从 Sparkle 官方发行包取得 Sparkle 2.7.3 或更高版本的 `Sparkle.framework` 和工具。首次发布运行 `bin/generate_keys` 生成更新签名密钥。构建时在原有 Qt 配置上添加以下 CMake 参数。

```text
-DSCREENCAPTURER_SPARKLE_FRAMEWORK=/absolute/path/to/Sparkle.framework
-DSCREENCAPTURER_SPARKLE_PUBLIC_KEY=<generate_keys 输出的 EdDSA 公钥>
-DSCREENCAPTURER_MAC_SIGN_IDENTITY=Developer ID Application: <名称> (<Team ID>)
```

构建 `buildOSXApp` 目标后，使用 `app/Final/ScreenCapturer.app` 进行签名验证和 Apple 公证。将公证票据装订到 `.app` 后再打包。以 `v1.5.1` 为例，在仅存放该版本更新包的目录中生成 appcast。

```sh
mkdir -p build/macos-release-v1.5.1
ditto -c -k --sequesterRsrc --keepParent app/Final/ScreenCapturer.app \
  build/macos-release-v1.5.1/ScreenCapturer-macos-universal.zip
/path/to/Sparkle/bin/generate_appcast \
  --download-url-prefix https://github.com/JackZ2024/ScreenCapturer/releases/download/v1.5.1/ \
  build/macos-release-v1.5.1
```

检查生成的 `appcast.xml` 中 `enclosure` 的版本、URL 和 `sparkle:edSignature`，然后将 `appcast.xml` 和 ZIP 一起上传到对应 Release。客户端读取最新正式 Release 中的 `appcast.xml`，所以之后每次设为 Latest 的 Release 也要带有该附件。如果当次只更新 Windows，把上一次的 `appcast.xml` 一并上传，保留其中指向旧 Release 的 macOS ZIP 链接。

没有配置 Sparkle 框架和公钥的 macOS 开发构建仍可启动，「检查更新」会显示当前构建未配置更新。
