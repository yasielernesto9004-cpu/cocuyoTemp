.section .rodata
.global overlay_payload_start
.type   overlay_payload_start, @object
.align  16
overlay_payload_start:
    .incbin "bin/overlay_payload.elf"
overlay_payload_end:

.global overlay_payload_size
.type   overlay_payload_size, @object
.align  4
overlay_payload_size:
    .int overlay_payload_end - overlay_payload_start
