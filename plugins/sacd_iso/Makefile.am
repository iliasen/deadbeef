if HAVE_SACD_ISO
pkglib_LTLIBRARIES = sacd_iso.la

sacd_iso_la_SOURCES = \
	sacd_iso.c \
	dsd2pcm.c \
	libsacd/scarletbook.c \
	libsacd/scarletbook_read.c \
	libsacd/scarletbook_helpers.c \
	libsacd/sacd_input.c \
	libsacd/sacd_reader.c \
	libsacd/dst_decoder.cpp \
	libsacd/dst2/decoder/decoder.cpp

sacd_iso_la_LDFLAGS = -module -avoid-version -export-symbols-regex sacd_iso_load

sacd_iso_la_LIBADD = $(LDADD) -lm
sacd_iso_la_CFLAGS = $(CFLAGS) -std=c99 -I@top_srcdir@/include -I@top_srcdir@/plugins/sacd_iso/libsacd
sacd_iso_la_CXXFLAGS = $(CXXFLAGS) -std=c++17 -I@top_srcdir@/include -I@top_srcdir@/plugins/sacd_iso/libsacd -I@top_srcdir@/plugins/sacd_iso/libsacd/dst2
endif