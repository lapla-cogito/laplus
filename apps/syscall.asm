bits 64
section .text

%macro define_syscall 2
global Syscall%1
Syscall%1:
    mov rax, %2
    mov r10, rcx
    syscall
    ret
%endmacro

define_syscall LogString,        0x80000000
define_syscall PutString,        0x80000001
define_syscall Exit,             0x80000002
define_syscall OpenWindow,       0x80000003
define_syscall WinWriteString,   0x80000004
define_syscall WinFillRectangle, 0x80000005
define_syscall GetCurrentTick,   0x80000006
define_syscall WinRedraw,        0x80000007
define_syscall WinDrawLine,      0x80000008
define_syscall CloseWindow,      0x80000009
define_syscall ReadEvent,        0x8000000a
define_syscall CreateTimer,      0x8000000b
define_syscall OpenFile,         0x8000000c
define_syscall ReadFile,         0x8000000d
define_syscall DemandPages,      0x8000000e
define_syscall MapFile,          0x8000000f
define_syscall SocketOpen,       0x80000010
define_syscall SocketClose,      0x80000011
define_syscall SocketIOCTL,      0x80000012
define_syscall SocketRecvFrom,   0x80000013
define_syscall SocketSendTo,     0x80000014
define_syscall SocketBind,       0x80000015
define_syscall SocketListen,     0x80000016
define_syscall SocketAccept,     0x80000017
define_syscall SocketConnect,    0x80000018
define_syscall SocketRecv,       0x80000019
define_syscall SocketSend,       0x8000001a
