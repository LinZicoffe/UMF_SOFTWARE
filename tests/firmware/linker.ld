/* 仅用于函数级回归。保留正式 App 起点/19KB RAM，不包含启动向量或固件头。 */
ENTRY(test_smoke_itoa)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08002400, LENGTH = 52K
    RAM (rw)   : ORIGIN = 0x20000000, LENGTH = 19K
}
SECTIONS
{
    .text : {
        KEEP(*(.text.test_*))
        KEEP(*(.text.probe_*))
        *(.text*) *(.rodata*)
    } > FLASH
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : { *(.data*) } > RAM AT> FLASH
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
    . = ALIGN(8);
    __ram_used_end = .;
    ASSERT(__ram_used_end < 0x20003C00, "test data overlaps reserved 4KB stack")
    /DISCARD/ : { *(.comment) *(.note*) }
}
