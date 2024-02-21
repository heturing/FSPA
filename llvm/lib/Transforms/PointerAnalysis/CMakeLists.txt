add_llvm_library( LLVMPointerAnalysis MODULE BUILDTREE_ONLY
  PointerAnalysis.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )