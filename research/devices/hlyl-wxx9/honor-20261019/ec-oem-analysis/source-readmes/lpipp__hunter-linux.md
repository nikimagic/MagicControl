# hunter-linux

HONOR Hunter / HUAWEI FRD-WX9 的 Linux 只读查询工具。版本 **0.3.0**，只提供两个命令：

```console
$ hunter-mode
1

$ hunter-fans
1962 1969
```

- `hunter-mode`：输出 `0`（均衡）、`1`（性能）或 `2`（安静），末尾换行。
- `hunter-fans`：按 fan1、fan2 顺序输出两路 RPM，空格分隔，末尾换行。

模式由电脑自带的性能按键切换。程序每次执行时查询固件当前值，没有模式缓存、后台轮询或事件监听；Waybar 等工具自行决定查询间隔。示例中的转速不是固定值。

## 实现

两个命令各为六行 POSIX sh 脚本，读取 C 驱动提供的两个只读节点：

```text
/sys/bus/platform/devices/huawei-wmi/hunter/mode
/sys/bus/platform/devices/huawei-wmi/hunter/fans
```

节点权限为 `0444`，普通用户可直接调用，不需要 sudo。读取失败时错误写入 stderr，退出码非零，stdout 不输出替代数值。两路风扇先后查询。

驱动扩展现有 `huawei-wmi`，模块名仍为 `huawei_wmi`，保留原 Huawei 热键、Fn lock、充电阈值和麦克风静音灯功能。新查询与原驱动共用完整事务锁，严格校验固件返回；Hunter 状态 1 最多重试一次，不重试传输或格式错误。驱动挂起时暂停查询，恢复后下一次调用重新查询。

本工具不提供性能设置、温度、JSON、D-Bus、polkit 或后台服务。

## 支持范围

目前已验证：

| 项目 | 版本 |
|---|---|
| 机型 | HUAWEI FRD-WX9 |
| 主板 / 产品版本 | FRD-WX9-PCB / M1060 |
| BIOS | 1.18 |
| 系统 / 内核 | Ubuntu 26.04 / 7.0.0-38-generic |

普通用户模式/风扇查询、并发查询及模块替换已在该配置上验证。完整休眠/恢复、安装后重启及其他内核或发行版尚未验证。未配置 DKMS，内核升级后需要适配并重新构建对应模块。

## 构建与安装

运行命令只依赖 `/bin/sh` 和 `cat`。构建需要 C 编译器、make、Python 3 以及与目标内核匹配的 headers；Python 只用于生成驱动源码和补丁。

在 Ubuntu 上安装构建依赖后，在仓库根目录执行：

```sh
sudo apt install build-essential python3 linux-headers-$(uname -r)
make test
make kernel CC=x86_64-linux-gnu-gcc
```

`make test` 运行 C 协议、格式、重试和事务并发测试，并检查两个 shell 脚本语法，不访问真实固件。

在上表对应的系统和硬件上，可用安装脚本部署：

```sh
sudo sh tools/install.sh
```

脚本安装两个命令和扩展模块，保留原发行版模块，更新当前内核的模块索引及启动镜像，再加载扩展。变更前的文件保存在 `/var/lib/hunter-readonly/backups/`；失败时尝试回滚。安装脚本目前限定 `7.0.0-38-generic`，需要 systemd、kmod、udev 和 initramfs-tools。模块加载遵循系统现有签名策略。

只暂存用户态命令而不修改系统：

```sh
make install DESTDIR="$PWD/build/package"
```

这个目标只安装两个脚本，查询仍需对应驱动提供 sysfs 节点。

## Waybar

将以下内容合并到已有配置，由 Waybar 定期调用命令：

```jsonc
{
  "modules-right": ["custom/hunter-mode", "custom/hunter-fans"],
  "custom/hunter-mode": {
    "exec": "hunter-mode",
    "interval": 2,
    "format": "模式 {}"
  },
  "custom/hunter-fans": {
    "exec": "hunter-fans",
    "interval": 5,
    "format": "风扇 {}"
  }
}
```

## 源码与许可证

项目使用 [GPL-2.0](LICENSE)。原 Huawei 驱动版权声明保留在源码中。

原始驱动来自 Ubuntu `linux 7.0.0-38.38`，位于 `kernel/vendor/huawei-wmi.c`，来源及哈希见 [sources.json](kernel/vendor/sources.json)。`tools/prepare_kernel.py` 在校验该原始文件后生成扩展驱动和完整补丁 `kernel/huawei-wmi-hunter.patch`。

Hunter 只读扩展修改于 2026-10-12，涉及共享事务处理、两个 sysfs 查询节点及生命周期协调。当前仓库仅包含此只读版本及构建所需的源码。
