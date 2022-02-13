extern "C" void KernelMain() {//エントリーポイント
  while (1) __asm__("hlt");
}