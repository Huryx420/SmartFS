#!/usr/bin/env python3
import os
import sys
import argparse
import subprocess
#import xattr # 需要安装: pip install xattr (或者直接调用系统命令)

# 如果没有 xattr 库，我们用 subprocess 调用系统命令 (兼容性更好)
def get_xattr(path, name):
    try:
        # result 格式: user.smartfs.versions="v1 | ..."
        out = subprocess.check_output(["getfattr", "-n", name, "--only-values", path], stderr=subprocess.DEVNULL)
        return out.decode('utf-8')
    except subprocess.CalledProcessError:
        return None

def set_xattr(path, name, value):
    try:
        subprocess.check_call(["setfattr", "-n", name, "-v", value, path])
        return True
    except subprocess.CalledProcessError:
        return False

# ================= 命令实现 =================

def cmd_list(args):
    """列出文件的所有历史版本"""
    path = args.file
    if not os.path.exists(path):
        print(f"Error: File '{path}' not found.")
        return

    raw_data = get_xattr(path, "user.smartfs.versions")
    if not raw_data:
        print(f"No version history found for '{path}' (or not a SmartFS file).")
        return

    print(f"=== Version History for {path} ===")
    print(f"{'Ver':<6} {'Pinned':<8} {'Time':<20} {'Size':<10} {'Message'}")
    print("-" * 60)
    
    # 解析我们在 C 语言里生成的字符串 (vN | date | msg | size)
    # C代码格式: "v1[PIN] | 2023... | msg | 10 bytes\n"
    lines = raw_data.strip().split('\n')
    for line in lines:
        parts = line.split('|')
        if len(parts) >= 4:
            vid_part = parts[0].strip()
            time_part = parts[1].strip()
            msg_part = parts[2].strip()
            size_part = parts[3].strip()
            
            # 处理 Pin 标记
            pinned = "YES" if "[PIN]" in vid_part else "-"
            vid = vid_part.replace("[PIN]", "")
            
            print(f"{vid:<6} {pinned:<8} {time_part:<20} {size_part:<10} {msg_part}")

def cmd_snapshot(args):
    """手动创建快照"""
    path = args.file
    msg = args.message
    print(f"Creating snapshot for '{path}'...")
    if set_xattr(path, "user.smartfs.snapshot", msg):
        print("Success! Snapshot created.")
    else:
        print("Failed. (File system full or all versions pinned?)")

def cmd_pin(args):
    """锁定或解锁指定版本"""
    path = args.file
    ver = args.version # e.g., "v1"
    print(f"Toggling pin status for version '{ver}' of '{path}'...")
    if set_xattr(path, "user.smartfs.pin", ver):
        print("Success! Pin status toggled.")
    else:
        print(f"Failed. Does version {ver} exist?")

def cmd_cat(args):
    """读取指定版本的内容"""
    # 组合路径: file.txt + @ + v1 -> file.txt@v1
    target = f"{args.file}@{args.version}"
    try:
        with open(target, 'r') as f:
            print(f.read(), end='')
    except FileNotFoundError:
        print(f"Error: Version '{target}' not found.")

def cmd_recover(args):
    """(选做) 恢复文件到指定版本"""
    # 逻辑：读取旧版本内容 -> 覆盖当前文件
    src = f"{args.file}@{args.version}"
    dst = args.file
    
    confirm = input(f"Are you sure you want to overwrite '{dst}' with content from '{src}'? [y/N] ")
    if confirm.lower() != 'y':
        print("Cancelled.")
        return

    try:
        with open(src, 'rb') as f_src:
            content = f_src.read()
        with open(dst, 'wb') as f_dst:
            f_dst.write(content)
        print(f"Recovered '{dst}' to version {args.version}.")
    except Exception as e:
        print(f"Recovery failed: {e}")

# ================= 主程序入口 =================

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SmartFS Management Tool")
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # Command: list
    p_list = subparsers.add_parser("list", help="List all versions of a file")
    p_list.add_argument("file", help="Path to the file")

    # Command: snapshot
    p_snapshot = subparsers.add_parser("snapshot", help="Create a manual snapshot")
    p_snapshot.add_argument("file", help="Path to the file")
    p_snapshot.add_argument("-m", "--message", default="Manual Snapshot", help="Commit message")

    # Command: pin
    p_pin = subparsers.add_parser("pin", help="Pin/Unpin a version (prevent auto-deletion)")
    p_pin.add_argument("file", help="Path to the file")
    p_pin.add_argument("version", help="Version ID (e.g., v1)")

    # Command: cat
    p_cat = subparsers.add_parser("cat", help="Display content of a historical version")
    p_cat.add_argument("file", help="Path to the file")
    p_cat.add_argument("version", help="Version specifier (e.g., v1, 2h, yesterday)")

    # Command: recover
    p_rec = subparsers.add_parser("recover", help="Rollback file to a previous version")
    p_rec.add_argument("file", help="Path to the file")
    p_rec.add_argument("version", help="Version ID to restore from (e.g., v1)")

    args = parser.parse_args()

    if args.command == "list":
        cmd_list(args)
    elif args.command == "snapshot":
        cmd_snapshot(args)
    elif args.command == "pin":
        cmd_pin(args)
    elif args.command == "cat":
        cmd_cat(args)
    elif args.command == "recover":
        cmd_recover(args)
    else:
        parser.print_help()