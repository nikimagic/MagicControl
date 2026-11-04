# honor-fan

荣耀 MagicBook Pro 16（2024，`DRA-XX`）在 Linux 下的风扇转速读取与控制。

## 上游

https://github.com/Little-King2022/honor-fan

## 安装

Secure Boot 会让内核进入 `lockdown=integrity` 模式，它封死了所有从用户态访问该硬件的
途径（`/dev/mem`、`ioperm()`、`ec_sys write_support=1`、未签名模块）。而**已签名的**
内核模块不受限制——这正是本驱动存在的原因。

```bash
git clone https://github.com/littleflit/honor-fan.git
cd honor-fan/kernel
make                  # clang 构建的内核（如 CachyOS）：make LLVM=1
```

> clang 构建的内核不能用 gcc 编模块。如果见到
> `gcc: error: unrecognized command-line option '-mllvm'` 之类报错，
> 说明你的内核是 clang 编的——改用 `make LLVM=1`（下面的 install 同理）。
> 所需 clang 版本与 `/proc/version` 里的一致即可。

### 保持 Secure Boot 开启（推荐）

```bash
sudo ./sign_and_enroll.sh     # 生成 MOK 密钥、签名模块、注册密钥
sudo reboot                   # 蓝色 MOK 界面：Enroll MOK -> Continue -> Yes -> 输入密码
```

注册 MOK **不会**关闭 Secure Boot。如果是双系统，动这块之前请先准备好 BitLocker
恢复密钥（`account.microsoft.com/devices/recoverykey`）。

每次内核更新后需重新运行 `sign_and_enroll.sh`；密钥会复用，无需再次注册。

### 已关闭 Secure Boot

不需要签名，直接 `sudo insmod honor_fan.ko`。

### 加载

```bash
sudo insmod honor_fan.ko                    # 仅监控
sudo insmod honor_fan.ko enable_control=1   # 同时允许调速
```

### 持久化

```bash
sudo make LLVM=1 install   # gcc 构建的内核直接 sudo make install
echo honor_fan | sudo tee /etc/modules-load.d/honor_fan.conf
echo "options honor_fan enable_control=1" | sudo tee /etc/modprobe.d/honor_fan.conf
```

滚动发行版内核更新频繁，每次大更新后回来重新 `make install` 一次
（模块针对当前运行内核的头文件构建）。

## 许可证

GPL-2.0，见 [LICENSE](LICENSE)。
