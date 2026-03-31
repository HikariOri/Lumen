set(KTX_FEATURE_TESTS OFF CACHE BOOL "Disable KTX unit tests in Lumen" FORCE)
add_subdirectory(ktx)

# spirv-cross
add_subdirectory(spirv-cross)

set_target_properties(spirv-cross PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-c PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-core PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-glsl PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-cpp PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-hlsl PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-msl PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-reflect PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
set_target_properties(spirv-cross-util PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)
