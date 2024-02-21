add_llvm_library( LLVMTestCallGraph MODULE BUILDTREE_ONLY
  TestCallGraph.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )