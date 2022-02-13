#include<cstdint>

//引数は順にブートローダーから渡されるフレームバッファの先頭アドレスとそのサイズ
extern "C" void KernelMain(uint64_t frame_buffer_base, uint64_t frame_buffer_size) {//エントリーポイント

	uint8_t* frame_buffer = reinterpret_cast<uint8_t*>(frame_buffer_base);//reinterpret_castはキャストの一種
	for (uint64_t i = 0; i < frame_buffer_size; ++i) { frame_buffer[i] = i % 256; }

	while (1) __asm__("hlt");
}