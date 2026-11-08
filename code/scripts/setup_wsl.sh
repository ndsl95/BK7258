#!/bin/bash
# WSL 环境准备（首次只跑一次）
set -e
echo '--- 1) 依赖'
sudo apt update
sudo apt install -y git curl build-essential cmake ninja-build python3 python3-pip libssl-dev dos2unix

echo '--- 2) pip 源（清华源已 403，用阿里云）'
printf '[global]\nindex-url = https://mirrors.aliyun.com/pypi/simple/\ntrusted-host = mirrors.aliyun.com\n' | sudo tee /etc/pip.conf

echo '--- 3) CRLF 转 LF（Windows clone 下来必须做）'
cd /root/bk7258
grep -rlI $'\r' . 2>/dev/null | xargs -r sed -i 's/\r$//'

echo '--- 4) 安装 Armino 工具链（非交互，SOC=bk7258）'
bash tools/env_tools/setup/armino_env_setup.sh -c bk7258 -q -t

echo '--- 完成，可以 make 了'