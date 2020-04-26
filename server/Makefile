obj-m += ss.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	g++ -O3 -Wall -Wno-write-strings -o ./dmm_proxy connections.cpp hints.cpp kernel_info.cpp meta_buffer.cpp \
	subflows.cpp proxy.cpp proxy_setting.cpp scheduler.cpp Storage.cpp tools.cpp -lpthread -lpcap
	echo "Build finish."

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm ./dmm_proxy
