#include "mac_file_dialog.h"

#include <string.h>

#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

static void build_allowed_types(const char* filters, NSMutableArray* out)
{
	if (!filters || !filters[0] || !out) return;
	char buf[512];
	strncpy(buf, filters, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	const char* delims = ";, ";
	char* token = strtok(buf, delims);
	while (token)
	{
		while (*token == '.') token++;
		if (*token)
		{
			NSString* ext = [NSString stringWithUTF8String:token];
			if (ext) [out addObject:ext];
		}
		token = strtok(NULL, delims);
	}
}
#endif

const char* mac_file_dialog(const char* title, const char* filters, const char* initial_dir)
{
#if !defined(__APPLE__)
	(void)title;
	(void)filters;
	(void)initial_dir;
	return NULL;
#else
	static char path[4096];
	path[0] = '\0';

	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
	NSOpenPanel* panel = [NSOpenPanel openPanel];
	[panel setCanChooseFiles:YES];
	[panel setCanChooseDirectories:NO];
	[panel setAllowsMultipleSelection:NO];
	if (title && title[0])
	{
		NSString* nsTitle = [NSString stringWithUTF8String:title];
		if (nsTitle) [panel setTitle:nsTitle];
	}

	NSMutableArray* types = [NSMutableArray array];
	build_allowed_types(filters, types);

	NSInteger result = 0;
	if (initial_dir && initial_dir[0])
	{
		NSString* dir = [NSString stringWithUTF8String:initial_dir];
		if (dir && [panel respondsToSelector:@selector(setDirectoryURL:)])
		{
			[panel setDirectoryURL:[NSURL fileURLWithPath:dir]];
		}
	}

	if ([panel respondsToSelector:@selector(setAllowedFileTypes:)])
	{
		if ([types count] > 0) [panel setAllowedFileTypes:types];
		result = [panel runModal];
	}
	else
	{
		NSString* dir = (initial_dir && initial_dir[0]) ? [NSString stringWithUTF8String:initial_dir] : nil;
		result = [panel runModalForDirectory:dir file:nil types:types];
	}

#if defined(NSModalResponseOK)
	if (result == NSModalResponseOK)
#else
	if (result == NSOKButton)
#endif
	{
		NSString* nsPath = nil;
		if ([panel respondsToSelector:@selector(URL)])
			nsPath = [[panel URL] path];
		else
			nsPath = [panel filename];

		if (nsPath)
		{
			const char* cpath = [nsPath fileSystemRepresentation];
			if (cpath)
			{
				strncpy(path, cpath, sizeof(path) - 1);
				path[sizeof(path) - 1] = '\0';
			}
		}
	}

	[pool drain];
	return (path[0] != '\0') ? path : NULL;
#endif
}
