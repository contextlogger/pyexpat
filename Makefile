.PHONY : web

web :
	cp -a ../tools/web/hiit.css web/
	../tools/bin/txt2tags --target xhtml --infile web/index.txt2tags.txt --outfile web/index.html --encoding utf-8 --verbose -C web/config.t2t

HTDOCS := ../contextlogger.github.com
PAGEPATH := pyexpat
PAGEHOME := $(HTDOCS)/$(PAGEPATH)
DLPATH := $(PAGEPATH)/download
DLHOME := $(HTDOCS)/$(DLPATH)
MKINDEX := ../tools/bin/make-index-page.rb

release :
	-mkdir -p $(DLHOME)
	cp -a patchfiles/* $(DLHOME)/
	rsync --times --verbose '--exclude=*_dev/*.sisx' '--exclude=*dev.sisx' '--exclude=pyexpat.*' build/*/*.sis build/*/*.sisx $(DLHOME)/
	$(MKINDEX) $(DLHOME)
	cp -a web/*.css $(PAGEHOME)/
	cp -a web/*.html $(PAGEHOME)/
	chmod -R a+rX $(PAGEHOME)

upload :
	cd $(HTDOCS) && git add $(PAGEPATH) && git commit -a -m updates && git push

-include local.mk
