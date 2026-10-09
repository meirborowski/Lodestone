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

# ls_forbid_dependencies(<target> <forbidden target>...)
#
# Fails the configure if <target> links any of the forbidden targets, directly or through its dependencies. It keeps
# the layering in docs/Architecture.md#targets-and-layering - such as LodestoneCore never linking GLFW - enforced by
# the build rather than by discipline. Call it after every target it walks is defined.
function(ls_forbid_dependencies target)
	set(pending ${target})
	set(visited "")
	while(pending)
		list(POP_FRONT pending current)
		if(current IN_LIST visited OR NOT TARGET ${current})
			continue()
		endif()
		list(APPEND visited ${current})

		get_target_property(aliased ${current} ALIASED_TARGET)
		if(aliased)
			list(APPEND pending ${aliased})
			continue()
		endif()

		get_target_property(type ${current} TYPE)
		set(links "")
		if(NOT type STREQUAL "INTERFACE_LIBRARY")
			get_target_property(directLinks ${current} LINK_LIBRARIES)
			if(directLinks)
				list(APPEND links ${directLinks})
			endif()
		endif()
		get_target_property(interfaceLinks ${current} INTERFACE_LINK_LIBRARIES)
		if(interfaceLinks)
			list(APPEND links ${interfaceLinks})
		endif()

		foreach(link IN LISTS links)
			# Static libraries list their private dependencies as $<LINK_ONLY:...>
			if(link MATCHES "^\$<(LINK_ONLY|BUILD_INTERFACE):([^>]+)>$")
				set(link "${CMAKE_MATCH_2}")
			elseif(link MATCHES "^\$<")
				continue()
			endif()
			list(APPEND pending ${link})
		endforeach()
	endwhile()

	foreach(forbidden IN LISTS ARGN)
		if(forbidden IN_LIST visited)
			message(FATAL_ERROR "${target} links ${forbidden}, which it must not (see docs/Architecture.md#targets-and-layering)")
		endif()
	endforeach()
endfunction()
