/* imxrt1062_cold.ld - the Teensy 4 core's imxrt1062.ld, plus rules that keep
 * cold code and its constant data in flash instead of RAM1.
 *
 * The core's script copies every function without a section attribute into
 * ITCM, the fast RAM1 that the stack shares (in 32 KB banks), and every
 * constant into DTCM. Most of that is library code that never runs in the
 * audio interrupt: newlib's printf/strtod/malloc, SdFat, Wire, EEPROM. The
 * COLD_* rules below place it, and this firmware's background-only files,
 * in flash, where it runs through the 32 KB instruction cache.
 *
 * Rules: an input section goes to the first output section whose pattern
 * matches, so these come before .text.itcm's catch-all. Never list a file
 * here that has code the audio interrupt runs (AudioGraph, Controls'
 * encoder interrupt, the effects, DrumMachine); check with
 * arm-none-eabi-nm that the interrupt's functions stay below 0x20000000.
 * libc's mem* stay in ITCM (memset runs in the interrupt), and so do its
 * str*: their unwind entries in .ARM.exidx (ITCM) can't reach flash. */

MEMORY
{
	ITCM (rx):  ORIGIN = 0x00000000, LENGTH = 512K
	DTCM (rw):  ORIGIN = 0x20000000, LENGTH = 512K
	RAM (rw):   ORIGIN = 0x20200000, LENGTH = 512K
	FLASH (rx): ORIGIN = 0x60000000, LENGTH = 1984K
}

ENTRY(ImageVectorTable)

SECTIONS
{
	.text.headers : {
		KEEP(*(.flashconfig))
		FILL(0xFF)
		. = ORIGIN(FLASH) + 0x1000;
		KEEP(*(.ivt))
		KEEP(*(.bootdata))
		. = ALIGN(1024);
	} > FLASH

	.text.code : {
		KEEP(*(.startup))
		*(.flashmem*)
		/* COLD_TEXT */
		*libc.a:*(EXCLUDE_FILE(*libc_a-mem*.o *libc_a-str*.o) .text*)
		*libSdFat.a:*(.text*)
		*libSD.a:*(.text*)
		*libSerialFlash.a:*(.text*)
		*libEEPROM.a:*(.text*)
		*libWire.a:*(.text*)
		*liboledui.a:*(.text*)
		*libirdsp.a:IRLoader.cpp.o(.text*)
		*libirdsp.a:MidiDrums.cpp.o(.text*)
		*libAudio.a:control_sgtl5000.cpp.o(.text*)
		*src/main.cpp.o(.text*)
		*src/SerialUI.cpp.o(.text*)
		*src/Settings.cpp.o(.text*)
		*src/DisplayUI.cpp.o(.text*)
		*src/IRLibrary.cpp.o(.text*)
		*src/DrumSongs.cpp.o(.text*)
		*src/Oled.cpp.o(.text*)
		. = ALIGN(4);
		KEEP(*(.init))
		__preinit_array_start = .;
		KEEP (*(.preinit_array))
		__preinit_array_end = .;
		__init_array_start = .;
		KEEP (*(.init_array))
		__init_array_end = .;
		. = ALIGN(4);
	} > FLASH

	.text.progmem : {
		*(.progmem*)
		/* COLD_RODATA: the same files' constants and string literals */
		*libc.a:*(.rodata*)
		*libSdFat.a:*(.rodata*)
		*libSD.a:*(.rodata*)
		*liboledui.a:*(.rodata*)
		*libirdsp.a:IRLoader.cpp.o(.rodata*)
		*libirdsp.a:MidiDrums.cpp.o(.rodata*)
		*src/main.cpp.o(.rodata*)
		*src/SerialUI.cpp.o(.rodata*)
		*src/Settings.cpp.o(.rodata*)
		*src/DisplayUI.cpp.o(.rodata*)
		*src/IRLibrary.cpp.o(.rodata*)
		*src/DrumSongs.cpp.o(.rodata*)
		*src/Oled.cpp.o(.rodata*)
		. = ALIGN(4);
	} > FLASH

	.text.itcm : {
		. = . + 32; /* MPU to trap NULL pointer deref */
		*(.fastrun)
		*(.text*)
		. = ALIGN(16);
	} > ITCM  AT> FLASH

	.ARM.exidx : {
		__exidx_start = .;
		*(.ARM.exidx* .ARM.extab.text* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > ITCM  AT> FLASH

	.data : {
    		*(.endpoint_queue)    
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.data*)))
   		 KEEP(*(.vectorsram))
	} > DTCM  AT> FLASH

	.bss ALIGN(4) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(COMMON)))
		. = ALIGN(32);
		. = . + 32; /* MPU to trap stack overflow */
	} > DTCM

	.bss.dma (NOLOAD) : {
		*(.hab_log)
		*(.dmabuffers)
		. = ALIGN(32);
	} > RAM

	.text.csf : {
		FILL(0xFF)
		. = ALIGN(1024);
		KEEP(*(.csf))
		__text_csf_end = .;
	} > FLASH

	_stext = ADDR(.text.itcm);
	_etext = ADDR(.text.itcm) + SIZEOF(.text.itcm) + SIZEOF(.ARM.exidx);
	_stextload = LOADADDR(.text.itcm);

	_sdata = ADDR(.data);
	_edata = ADDR(.data) + SIZEOF(.data);
	_sdataload = LOADADDR(.data);

	_sbss = ADDR(.bss);
	_ebss = ADDR(.bss) + SIZEOF(.bss);

	_heap_start = ADDR(.bss.dma) + SIZEOF(.bss.dma);
	_heap_end = ORIGIN(RAM) + LENGTH(RAM);

	_itcm_block_count = (SIZEOF(.text.itcm) + SIZEOF(.ARM.exidx) + 0x7FFF) >> 15;
	_flexram_bank_config = 0xAAAAAAAA | ((1 << (_itcm_block_count * 2)) - 1);
	_estack = ORIGIN(DTCM) + ((16 - _itcm_block_count) << 15);

	_flashimagelen = __text_csf_end - ORIGIN(FLASH);
	_teensy_model_identifier = 0x24;

	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }

}
