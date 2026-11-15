# 基础标准库重建

`BuildStandardLibrary.py` 使用显式指定候选核心中的编译器和头文件，重编原始 `__memcpy.c`、`_memset.c`、`strlen.c`。公开接口仍是普通 ABI2、三字节泛型指针和四字节 `size_t`；编译选项为 stack-auto、ordinary-wide、Machine IR、size。

```powershell
python -B core/sources/standard-library-builder/BuildStandardLibrary.py --core E:/path/to/candidate/core --output E:/path/to/new-library-build
```

先复制需要更新的核心作为 candidate；命令会更新指定核心。`--source` 可指定标准库源码目录，默认使用配方旁的 `../compiler/device/lib`。输出目录须为新目录，保留两种内存模型的命令、汇编、目标文件、归档和哈希记录。

每种模型的 libsdcc 仅替换这三个成员；原有导出符号一致，其余成员逐字节保持不变。脚本同步更新启动库的归档/成员哈希、格式化库的归档哈希，以及全部目标配置中的工具链锁。源码、配方和生成记录随候选核心保存，`core.json.standardLibrary` 标识本次生成来源。

配方不引用工程编号、SDK 或器件型号。现有非 stack-auto 库不在本次范围内；应用自定义的函数仍由正常的符号解析顺序选择。

编译成功只产生 `COMPILE_ONLY` 候选，父版本的实板验证不能继承给重编后的函数。独立测试入口为 `tools/research/standard_library_fixture.py`，包含 DATA、EDATA、XDATA、CODE、零长度、跨 255/256 长度边界、返回指针、前后哨兵和填充值截断。主机执行用于检查测试逻辑，目标执行应使用生成的固件和串口帧。

本批范围固定为以上三个成员，不自动扩展到其它标准库函数。
