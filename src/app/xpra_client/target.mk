TARGET = xpra_client
SRC_CC = main.cc bencode_decode.cc bencode_encode.cc keyboard.cc client.cc protocol.cc
LIBS  += base
LIBS  += libc
LIBS  += lz4  # Xpra compression support
LIBS  += zlib # Xpra compression support
LIBS  += blit # used by nitpicker_gfx/Texture_painter
