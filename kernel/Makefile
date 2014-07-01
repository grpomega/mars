#
# Makefile for MARS
#

obj-$(CONFIG_MARS)	+= mars.o

KBUILD_CFLAGS += -fdelete-null-pointer-checks

mars-objs :=				\
	lamport.o			\
	brick_say.o			\
	brick_mem.o			\
	brick.o				\
	xio_bricks/xio.o			\
	xio_bricks/lib_log.o			\
	lib_rank.o			\
	lib_limiter.o			\
	lib_timing.o			\
	xio_bricks/lib_mapfree.o			\
	xio_bricks/xio_net.o			\
	xio_bricks/xio_server.o			\
	xio_bricks/xio_client.o			\
	xio_bricks/xio_aio.o			\
	xio_bricks/xio_bio.o			\
	xio_bricks/xio_if.o			\
	xio_bricks/xio_copy.o			\
	xio_bricks/xio_trans_logger.o		\
	mars_light/light_strategy.o		\
	mars_light/light_net.o			\
	mars_light/mars_proc.o		\
	mars_light/mars_light.o

ifdef CONFIG_DEBUG_KERNEL

KBUILD_CFLAGS += -fno-inline-functions -fno-inline-small-functions -fno-inline-functions-called-once

endif

