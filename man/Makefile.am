#

dist_man_MANS = vmod_var.3
MAINTAINERCLEANFILES = $(dist_man_MANS)
EXTRA_DIST = vmod_var.rst

vmod_var.3: vmod_var.rst
if HAVE_RST2MAN
	${RST2MAN} vmod_var.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
