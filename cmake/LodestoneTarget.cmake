# Settings shared by every Lodestone target: engine libraries, executables and tests.
# Third-party targets never get these - their warnings aren't ours to fix (see docs/CodeStyle.md#enforcement).

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
		)
	endif()
endfunction()
