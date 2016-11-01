dir:=src

all : update

download :
	rm -rf $(dir)
	git clone https://github.com/massivethreads/massivethreads.git $(dir)

update :
ifneq ($(wildcard $(dir)/.git),)
	cd $(dir); git pull
else
	rm -rf $(dir)
	git clone https://github.com/massivethreads/massivethreads.git $(dir)
endif

distclean:
	rm -rf $(dir)

