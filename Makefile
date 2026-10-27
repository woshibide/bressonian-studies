# Attempt to load a config.make file.
# If none is found, project defaults in config.project.make will be used.
ifneq ($(wildcard config.make),)
	include config.make
endif

# make sure the the OF_ROOT location is defined
ifndef OF_ROOT
	OF_ROOT=../../..
endif

# call the project makefile!
include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk

# Project shortcuts. Run `make dev` after a code change and `make render` to render the queue.
APP_EXE = bin/$(APPNAME).app/Contents/MacOS/$(APPNAME)
RENDER_ARGS = $(if $(PARALLEL),--jobs $(PARALLEL)) $(if $(FRAMES),--frames $(FRAMES)) \
	$(if $(ONLY),--only $(ONLY)) $(if $(OUT),--out $(OUT)) $(if $(SPECS),--specs $(SPECS))

.PHONY: dev stop render

# Rebuild, quit the interactive app, and relaunch it. Headless renders keep running.
dev: Release stop
	@$(APP_EXE) > obj/app.log 2>&1 &
	@echo "launched $(APPNAME); log: obj/app.log"

# Matches only the interactive instance, which has no arguments.
stop:
	@pkill -f '$(APPNAME).app/Contents/MacOS/$(APPNAME)$$' || true

# Render every spec in bin/data/render-specs.toml to MP4 without opening a window.
# Each run writes to a new bin/data/renders/inverted-motion-trails/video/<timestamp>/ folder.
# Options: PARALLEL=N (specs at once), FRAMES=N (limit per spec), ONLY=video or video/spec,
# OUT=directory, SPECS=path/to/queue.toml.
# The build log only prints when the build fails, so the status table stays readable.
render:
	@echo "building $(APPNAME)..."
	@$(MAKE) Release > obj/build.log 2>&1 || { tail -40 obj/build.log; exit 1; }
	@$(APP_EXE) --render $(RENDER_ARGS)
