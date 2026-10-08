# Settings shared by every Lodestone target: engine libraries, executables and tests.
# Third-party targets never get these - their warnings aren't ours to fix (see docs/CodeStyle.md#enforcement).

include(CheckCXXCompilerFlag)
if(NOT MSVC)
	# Designated initializers may leave out members that have default member initializers - the way options structs
	# such as LogConfig are meant to be filled in. Clang 19+ warns about that under its own flag; GCC only has the
	# broader -Wmissing-field-initializers
	check_cxx_compiler_flag(-Wmissing-designated-field-initializers LS_HAS_DESIGNATED_FIELD_INITIALIZERS_WARNING)
	if(LS_HAS_DESIGNATED_FIELD_INITIALIZERS_WARNING)
		set(LS_ALLOW_PARTIAL_DESIGNATED_INITIALIZERS -Wno-missing-designated-field-initializers)
	else()
		set(LS_ALLOW_PARTIAL_DESIGNATED_INITIALIZERS -Wno-missing-field-initializers)
	endif()
endif()

# ls_configure_target(<target>)
#
# Builds the target's code with warnings as errors and standard-conforming compiler modes. Call it for every target
# that compiles Lodestone code.
function(ls_configure_target target)
	if(MSVC)
		target_compile_options(${target} PRIVATE
			/W4 /WX
			# Standard-conforming behaviour
			/permissive- /Zc:preprocessor /Zc:inline /Zc:throwingNew
			# C4702 (unreachable code) comes from the optimizer. With Dist's link-time code generation it's reported
			# for third-party code inlined into ours (sol2), where external-header warning suppression no longer applies
			$<$<CONFIG:Dist>:/wd4702>
		)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror
			-Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
			${LS_ALLOW_PARTIAL_DESIGNATED_INITIALIZERS}
		)
	endif()
endfunction()
