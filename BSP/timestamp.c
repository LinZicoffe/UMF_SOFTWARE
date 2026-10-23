/* 两种工具链均保留构建时间，避免未引用常量被链接器裁剪。 */
#if defined(__ICCARM__)
__root const char g_firmware_build_time[] = __DATE__ " " __TIME__;
#elif defined(__GNUC__)
__attribute__((used, section(".rodata.firmware_build_time")))
const char g_firmware_build_time[] = __DATE__ " " __TIME__;
#endif
