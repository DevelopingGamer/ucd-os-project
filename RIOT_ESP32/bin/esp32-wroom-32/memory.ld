       
       
MEMORY
{
  iram0_0_seg (RX) : org = 0x40080000, len = 0x20000 + 0x0
  iram0_2_seg (RX) : org = 0x400D0020, len = 0x330000-0x20
  dram0_0_seg (RW) : org = 0x3FFB0000 + 0,
                                     len = 0x2c200 - 0
  drom0_0_seg (R) : org = 0x3F400020, len = 0x400000-0x20
  rtc_iram_seg(RWX) : org = 0x400C0000, len = 0x2000 - 0x18
  rtc_data_seg(RW) : org = 0x3ff80000, len = 0x2000 - 0x18
  rtc_fast_reserved_seg(RW) : org = 0x3ff80000 + 0x2000 - 0x18, len = 0x18
  rtc_slow_seg(RW) : org = 0x50000000, len = 0x2000 - 0x18
  rtc_slow_reserved_seg(RW) : org = 0x50000000 + 0x2000 - 0x18, len = 0x18
  extern_ram_seg(RWX) : org = 0x3F800000,
                                     len = 0x400000
}
_heap_start = _heap_low_start;
_sram1_iram_start = 0x400A0000;
_sram1_iram_len = ( _iram_end > _sram1_iram_start) ? (_iram_end - _sram1_iram_start) : 0;
_heap_end = ALIGN(0x40000000 - _sram1_iram_len - 3, 4);
_data_seg_org = ORIGIN(rtc_data_seg);
REGION_ALIAS("rtc_data_location", rtc_slow_seg );
  REGION_ALIAS("default_code_seg", iram0_2_seg);
  REGION_ALIAS("default_rodata_seg", drom0_0_seg);
  ASSERT(_rodata_start == ORIGIN(default_rodata_seg),
         ".flash.appdesc section must be placed at the beginning of the rodata segment.")
