# File generated by kdevelop's qmake manager. 
# ------------------------------------------- 
# Subdir relative project main directory: ./yade/Geometry
# Target is a library:  

LIBS += -rdynamic 
INCLUDEPATH = $(YADEINCLUDEPATH) 
MOC_DIR = $(YADECOMPILATIONPATH) 
UI_DIR = $(YADECOMPILATIONPATH) 
OBJECTS_DIR = $(YADECOMPILATIONPATH) 
QMAKE_LIBDIR = $(YADEDYNLIBPATH) 
DESTDIR = $(YADEDYNLIBPATH) 
CONFIG += debug \
          warn_on \
          dll 
TEMPLATE = lib 
HEADERS += BoundingVolume.hpp \
           CollisionGeometry.hpp \
           CollisionGeometryFactory.hpp \
           GeometricalModel.hpp \
           GeometricalModelFactory.hpp \
           RenderingEngine.hpp \
           BoundingVolumeFactory.hpp \
           BoundingVolumeUpdator.hpp \
           BoundingVolumeFactory.ipp \
           BoundingVolumeFactoryManager.hpp 
SOURCES += BoundingVolume.cpp \
           CollisionGeometry.cpp \
           CollisionGeometryFactory.cpp \
           GeometricalModel.cpp \
           GeometricalModelFactory.cpp \
           BoundingVolumeFactory.cpp \
           BoundingVolumeUpdator.cpp \
           BoundingVolumeFactoryManager.cpp 
