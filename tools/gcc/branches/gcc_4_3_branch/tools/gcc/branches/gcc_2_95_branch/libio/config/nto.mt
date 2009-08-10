# Flags to pass to gen-params when building _G_config.h.
# For example: G_CONFIG_ARGS = size_t="unsigned long"
G_CONFIG_ARGS = size_t="unsigned long" off_t="unsigned long" \
	clock_t="unsigned int" dev_t="unsigned int" \
	ino_t="int" mode_t="unsigned int" nlink_t="unsigned int" \
	time_t="int" wchar_t="unsigned short" wint_t="unsigned short" \
	fpos_t="unsigned long"

MT_CFLAGS = -fno-honor-std
