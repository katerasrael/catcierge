//
// This file is part of the Catcierge project.
//
// Copyright (c) Joakim Soderberg 2013-2015
//
//    Catcierge is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    Catcierge is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with Catcierge.  If not, see <http://www.gnu.org/licenses/>.
//

#include "catcierge_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef CATCIERGE_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef CATCIERGE_HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef CATCIERGE_HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "catcierge_log.h"
#include "catcierge_util.h"
#include "catcierge_types.h"
#include "catcierge_output.h"
#include "catcierge_output_types.h"
#include "catcierge_fsm.h"
#include "catcierge_strftime.h"

#ifdef WITH_ZMQ
#include <czmq.h>
#endif

// TODO: Enable generating relative paths to a given path at the head of a template.

catcierge_output_var_t vars[] =
{
	{ "state", "The current state machine state." },
	{ "prev_state", "The previous state machine state."},
	{ "matcher", "The matching algorithm used."},
	{ "matchtime", "Value of --matchtime."},
	{ "ok_matches_needed", "Value of --ok_matches_needed" },
	{ "no_final_decision", "Value of --no_final_decision" },
	{ "lockout_method", "Value of --lockout_method." },
	{ "lockout_time", "Value of --lockout_time." },
	{ "lockout_error", "Value of --lockout_error." },
	{ "lockout_error_delay", "Value of --lockout_error_delay."},
	{ "output_path", "The output path specified via --output_path." },
	{ "match_output_path", "The output path specified via --match_output_path." },
	{ "steps_output_path", "The output path specified via --steps_output_path." },
	{ "obstruct_output_path", "The output path specified via --obstruct_output_path." },
	{ "template_output_path", "The output path specified via --template_output_path." },
	{ "match_group_id", "Match group ID."},
	{ "match_group_start_time", "Match group start time."},
	{ "match_group_end_time", "Match group end time."},
	{ "match_group_success", "Match group success status. 1 or 0"},
	{ "match_group_success_str", "Match group success status, as a string 'success' or 'fail'."},
	{ "match_group_success_count", "Match group success count."},
	{ "match_group_final_decision", "Did the match group veto the final decision?"},
	{ "match_group_desc", "Match group description."},
	{ "match_group_direction", "The match group direction (based on all match directions)."},
	{ "match_group_count", "Match group count o matches so far."},
	{ "match_group_max_count", "Match group max number of matches that will be made."},
	{ "obstruct_filename", "Filename for the obstruct image for the current match group." },
	{ "obstruct_path", "Path for the obstruct image (excluding filename)."},
	{ "matchcur_*", "Gets the current match while matching. "},
	{ "match#_idx", "Gets the current match index, that is #. Makes sense to use with matchcur_*"},
	{ "match#_id", "Unique ID for match #." },
	{ "match#_filename", "Image filenamefor match #." },
	{ "match#_path", "Image output path for match # (excluding filename)." },
	{ "match#_success", "Success status for match #." },
	{ "match#_direction", "Direction for match #." },
	{ "match#_description", "Description of match #." },
	{ "match#_result", "Result for match #." },
	{ "match#_time", "Time of match #." },
	{ "match#_step#_filename", "Image filename for match step # for match #."},
	{ "match#_step#_path", "Image path for match step # for match # (excluding filename)."},
	{ "match#_step#_name", "Short name for match step # for match #."},
	{ "match#_step#_desc", "Description for match step # for match #."},
	{ "match#_step#_active", "If this match step was used for match #."},
	{ "match#_step_count", "The number of match steps for match #."},
	{ "time", "The current time when generating template." },
	{
		"time:<fmt>",
		"The time using the given format string"
		"using strftime formatting (replace % with @ or &)."
		#ifdef _WIN32
		" Note that Windows only supports a subset of formatting characters."
		#endif // _WIN32
	},
	{ "git_hash", "The git commit hash for this build of catcierge."},
	{ "git_hash_short", "The short version of the git commit hash."},
	{ "git_tainted", "Was the git working tree changed when building."},
	{ "version", "The catcierge version." },
	{ "cwd", "Current working directory." },
	{ "template_path", "Path to the first template in the list (makes sense if you're only using one)." },
	{ "template_path:<name>", "Get the template with the given name (if multiple templates are used)." }
};

void catcierge_output_print_usage()
{
	size_t i;

	fprintf(stderr, "Output template variables:\n");

	for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++)
	{
		fprintf(stderr, "%30s   %s\n", vars[i].name, vars[i].description);
	}
}

int catcierge_output_init(catcierge_output_t *ctx)
{
	assert(ctx);
	memset(ctx, 0, sizeof(catcierge_output_t));
	ctx->template_max_count = 10;
	ctx->template_idx = -1;

	if (!(ctx->templates = calloc(ctx->template_max_count,
		sizeof(catcierge_output_template_t))))
	{
		CATERR("Out of memory\n"); return -1;
	}

	return 0;
}

void catcierge_output_free_template_settings(catcierge_output_settings_t *settings)
{
	#ifdef WITH_ZMQ
	catcierge_xfree(&settings->topic);
	#endif

	catcierge_free_list(settings->event_filter, settings->event_filter_count);
	settings->event_filter = NULL;
	settings->event_filter_count = 0;

	catcierge_xfree(&settings->filename);
	catcierge_xfree(&settings->name);
}

void catcierge_output_free_template(catcierge_output_template_t *t)
{
	if (!t)
		return;

	catcierge_xfree(&t->tmpl);
	catcierge_xfree(&t->name);
	catcierge_xfree(&t->generated_path);

	catcierge_output_free_template_settings(&t->settings);
}

void catcierge_output_destroy(catcierge_output_t *ctx)
{
	catcierge_output_template_t *t;
	assert(ctx);

	if (ctx->templates)
	{
		size_t i;

		for (i = 0; i < ctx->template_count; i++)
		{
			t = &ctx->templates[i];
			catcierge_output_free_template(t);
		}

		free(ctx->templates);
		ctx->templates = NULL;
	}

	ctx->template_count = 0;
	ctx->template_max_count = 0;
}

int catcierge_output_read_event_setting(catcierge_output_settings_t *settings, const char *events)
{
	assert(settings);

	CATLOG("    Event filters: %s\n", events);

	if (settings->event_filter)
	{
		catcierge_free_list(settings->event_filter, settings->event_filter_count);
	}

	if (!(settings->event_filter = catcierge_parse_list(events, &settings->event_filter_count, 1)))
	{
		return -1;
	}

	return 0;
}

const char *catcierge_output_read_template_settings(catcierge_output_settings_t *settings, const char *template_str)
{
	const char *s = NULL;
	char *it = NULL;
	char *row_start = NULL;
	char *row_end = NULL;
	char *end = NULL;
	char *tmp = NULL;
	size_t bytes_read;
	assert(template_str);

	if (!(tmp = strdup(template_str)))
	{
		CATERR("Out of memory!\n"); return NULL;
	}

	it = tmp;
	row_start = it;
	row_end = it;
	end = it + strlen(it);

	// Consume all the settings in the file.
	while (it < end)
	{
		if (it == row_end)
		{
			// On the first row_end and row_start
			// will be the same. Otherwise we have
			// reached the replaced \n, so advance
			// past it.
			if (it != row_start)
				it++;

			it = catcierge_skip_whitespace_alt(it);
			row_start = it;

			if ((row_end = strchr(it, '\n')))
			{
				*row_end = '\0';
			}
			else
			{
				break;
			}
		}

		// Break as soon as we find a row without a setting.
		if (strncmp(it, "%!", 2))
		{
			break;
		}

		it += 2;
		it = catcierge_skip_whitespace_alt(it);

		// TODO: Enable listing these events in help by making a list of them.
		if (!strncmp(it, "event", 5))
		{
			it += 5;
			it = catcierge_skip_whitespace_alt(it);

			if (catcierge_output_read_event_setting(settings, it))
			{
				CATERR("Failed to parse event setting\n"); goto fail;
			}
			it = row_end;
			continue;
		}
		else if (!strncmp(it, "name", 4))
		{
			it += 4;
			it = catcierge_skip_whitespace_alt(it);
			catcierge_xfree(&settings->name);
			if (!(settings->name = strdup(it)))
			{
				CATERR("Out of memory!\n"); goto fail;
			}
			it = row_end;
			continue;
		}
		else if (!strncmp(it, "filename", 8))
		{
			it += 8;
			it = catcierge_skip_whitespace_alt(it);

			catcierge_xfree(&settings->filename);
			if (!(settings->filename = strdup(it)))
			{
				CATERR("Out of memory!\n"); goto fail;
			}

			it = row_end;
			continue;
		}
		else if (!strncmp(it, "nop", 3))
		{
			// So we can test the logic for 2 settings for now.
			it += 3;
			it = catcierge_skip_whitespace_alt(it);
			continue;
		}
		else if (!strncmp(it, "topic", 5))
		{
			// ZMQ topic.
			it += 5;
			it = catcierge_skip_whitespace_alt(it);

			#ifdef WITH_ZMQ
			catcierge_xfree(&settings->topic);
			if (!(settings->topic = strdup(it)))
			{
				CATERR("Out of memory!\n"); goto fail;
			}

			if (strlen(settings->topic) == 0)
			{
				CATERR("Empty topic specified in template\n"); goto fail;
			}
			#endif

			it = row_end;
			continue;
		}
		else if (!strncmp(it, "nozmq", 5))
		{
			// Don't publish this template to ZMQ.
			it += 5;
			it = catcierge_skip_whitespace_alt(it);
			#ifdef WITH_ZMQ
			settings->nozmq = 1;
			#endif
			it = row_end;
			continue;
		}
		else if (!strncmp(it, "nofile", 6))
		{
			// Don't write this template to file.
			it += 6;
			it = catcierge_skip_whitespace_alt(it);
			settings->nofile = 1;
			it = row_end;
			continue;
		}
		else if (!strncmp(it, "rootpath", 8))
		{
			// Path that all paths should be relative to.
			it += 8;
			it = catcierge_skip_whitespace_alt(it);

			catcierge_xfree(&settings->rootpath);
			if (!(settings->rootpath = strdup(it)))
			{
				CATERR("Out of memory!\n"); goto fail;
			}

			it = row_end;
			continue;
		}
		else
		{
			const char *unknown_end = strchr(it, '\n');
			int line_len = unknown_end ? (int)(unknown_end - it) : strlen(it);
			CATERR("Unknown template setting: \"%*s\"\n", line_len, it);
			it += line_len;
			goto fail;
		}
	}

	bytes_read = it - tmp;

	if (settings->event_filter_count == 0)
	{
		CATERR("!!! Output template \"%s\" missing event filter, nothing will be generated !!!\n", settings->name);
	}

	#ifdef WITH_ZMQ
	// Default for ZMQ publish topic to be the same as the template name.
	if (!settings->topic)
	{
		if (!(settings->topic = strdup(settings->name)))
		{
			CATERR("Out of memory!\n"); goto fail;
		}
	}
	#endif

	free(tmp);

	return template_str + bytes_read;
fail:
	if (tmp)
	{
		free(tmp);
	}

	catcierge_output_free_template_settings(settings);

	return NULL;
}

int catcierge_output_add_template(catcierge_output_t *ctx,
		const char *template_str, const char *filename)
{
	const char *path;
	catcierge_output_template_t *t;
	assert(ctx);

	// Get only the filename.
	if ((path = strrchr(filename, catcierge_path_sep()[0])))
	{
		filename = path + 1;
	}

	// Grow the templates array if needed.
	if (ctx->template_max_count <= ctx->template_count)
	{
		ctx->template_max_count *= 2;

		if (!(ctx->templates = realloc(ctx->templates,
			ctx->template_max_count * sizeof(catcierge_output_template_t))))
		{
			CATERR("Out of memory!\n"); return -1;
		}
	}

	t = &ctx->templates[ctx->template_count];
	memset(t, 0, sizeof(*t));

	// If the target template filename starts with
	// [name]bla_bla_%stuff%.json
	// The template will get the "name" inside the [].
	// Otherwise, simply use the template index as the name.
	// This is so that we can pass the path of the generated
	// target path at run time to the catcierge_execute function
	// and the external program can distinguish between multiple templates.
	{
		char name[4096];
		memset(name, 0, sizeof(name));

		if (sscanf(filename, "[%[^]]", name) == 1)
		{
			size_t name_len = strlen(name);

			if (name_len >= sizeof(name))
			{
				CATERR("Template name \"%s\" is too long, max %d characters allowed\n",
					name, sizeof(name));
				goto fail;
			}

			filename += 1 + name_len + 1; // Name + the []
		}
		else
		{
			snprintf(name, sizeof(name) - 1, "%d", (int)ctx->template_count);
		}

		if (!(t->settings.name = strdup(name)))
		{
			goto out_of_memory;
		}
	}

	// Default to the template filenames filename 
	// (which can include output variables that can be expanded).
	// However, the settings might override this.
	if (!(t->settings.filename = strdup(filename)))
	{
		goto out_of_memory;
	}

	if (!(template_str = catcierge_output_read_template_settings(&t->settings, template_str)))
	{
		goto fail;
	}

	if (!(t->name = strdup(t->settings.name)))
	{
		goto out_of_memory;
	}

	if (!(t->tmpl = strdup(template_str)))
	{
		goto out_of_memory;
	}

	ctx->template_count++;

	CATLOG(" %s (%s)\n", t->name, t->settings.filename);

	return 0;

out_of_memory:
	CATERR("Out of memory\n");
fail:
	catcierge_output_free_template(t);

	return -1;
}

static char *catcierge_replace_time_format_char(char *fmt)
{
	char *s;
	s = fmt;

	while (*s)
	{
		if ((*s == '@') || (*s == '&'))
			*s = '%';
		s++;
	}

	return fmt;
}

static char *catcierge_get_time_var_format(char *var,
	char *buf, size_t bufsize, const char *default_fmt, time_t t, struct timeval *tv)
{
	int ret;
	char *fmt = NULL;
	char *var_fmt = var + 4;
	assert(!strncmp(var, "time", 4));

	if (*var_fmt == ':')
	{
		char *s = NULL;
		var_fmt++;

		if (!(fmt = strdup(var_fmt)))
		{
			CATERR("Out of memory!\n"); return NULL;
		}

		catcierge_replace_time_format_char(fmt);
	}
	else
	{
		if (!(fmt = strdup(default_fmt)))
		{
			CATERR("Out of memory!\n"); return NULL;
		}
	}

	ret = catcierge_strftime(buf, bufsize - 1, fmt, localtime(&t), tv);

	if (!ret)
	{
		CATERR("Invalid time formatting string \"%s\"\n", fmt);
		buf = NULL;
	}

	if (fmt)
	{
		free(fmt);
	}

	return buf;
}

static char *catcierge_get_template_path(catcierge_grb_t *grb, const char *var)
{
	size_t i;
	catcierge_output_t *o = &grb->output;
	const char *subvar = var + strlen("template_path");
	const char *subvar_end = NULL;

	if (*subvar == ':')
	{
		subvar++;

		// To allow path operations the name stops at |
		subvar_end = strrchr(subvar, '|');
		if (subvar_end) subvar_end--;
		else subvar_end = subvar + strlen(subvar);

		// Find the template with the given name.
		for (i = 0; i < o->template_count; i++)
		{
			if (!strncmp(subvar, o->templates[i].name, (subvar_end - subvar)))
			{
				return o->templates[i].generated_path;
			}
		}
	}
	else
	{
		// If no template name is given, simply use the first one.
		// (This will probably be the most common case).
		if (o->template_count > 0)
		{
			return o->templates[0].generated_path;
		}
	}

	return NULL;
}

static char *catcierge_get_short_id(char *subvar, char *buf, size_t bufsize, SHA1Context *sha)
{
	int n;
	int ret;
	assert(sha);

	ret = snprintf(buf, bufsize - 1, "%x%x%x%x%x",
			sha->Message_Digest[0],
			sha->Message_Digest[1],
			sha->Message_Digest[2],
			sha->Message_Digest[3],
			sha->Message_Digest[4]);

	if (*subvar == ':')
	{
		subvar++;
		n = atoi(subvar);

		if (n < 0)
			return NULL;

		if (n >= ret)
			n = ret;

		buf[n] = '\0';
	}

	return buf;
}

static char *catcierge_get_path(catcierge_grb_t *grb, const char *var,
								catcierge_path_t *path,
								char *buf, size_t bufsize)
{
	// Example:
	// cwd         = /tut
	// output_path = abc/def
	//
	//  obstruct_path          -> abc/def/file.png
	//  obstruct_path|dir      -> abc/def/
	//  obstruct_path|abs      -> /tut/abc/def/file.png
	//  bla = /bla/teet
	//  obstruct_path|rel(@bla@/omg) -> ../../tut/bla/def/file.png
	//  obstruct_path|dir,abs  -> /tut/abc/def

	const char *path_ops = strchr(var, '|');
	int is_dir = 0;
	int is_abs = 0;
	int is_rel = 0;
	char *the_path = NULL;
	char *rel_to_path = NULL;
	catcierge_output_t *ctx = NULL;
	catcierge_output_template_t *template = NULL;
	size_t count = 0;
	char **path_ops_list = NULL;
	assert(path);
	assert(grb);

	ctx = &grb->output;

	// If we are generating this based on a template
	// we want the settings from the template so we
	// can use the rootpath for deciding the relative path.
	if (ctx->template_idx > 0)
	{
		template = &ctx->templates[ctx->template_idx];
	}

	if (!(*path->full))
	{
		char *dir_end = path->dir + strlen(path->dir) - 1;

		snprintf(path->full, sizeof(path->full) - 1, "%s%s%s",
			*path->dir ? path->dir : "",
			(*dir_end == catcierge_path_sep()[0]) ? "" : catcierge_path_sep(),
			*path->filename ? path->filename : "");
	}

	// Get any path operations if any.
	if (path_ops)
	{
		size_t i = 0;
		char *c = NULL;

		path_ops++; // Skip the | char.

		// First we parse each path operation.
		// We wait with performing the actual action until later.
		path_ops_list = catcierge_parse_list(path_ops, &count, 1);

		for (i = 0; i < count; i++)
		{
			if (!strcmp(path_ops_list[i], "dir"))
			{
				is_dir = 1;
			}
			else if (!strcmp(path_ops_list[i], "abs"))
			{
				is_abs = 1;
			}
			else if (!strncmp(path_ops_list[i], "rel", 3))
			{
				//
				// Parse/evaluate the path we should calculate the relative path.
				//

				char *s = path_ops_list[i];
				char *start = NULL;

				if (rel_to_path)
				{
					CATERR("Double parse on relative path '%s'\n", var);
					goto fail;
				}

				s += sizeof("rel") - 1;

				if (!(s = strchr(s, '('))) return NULL;
				s++;
				start = s;

				// The nested path uses @ instead of % as not to confuse
				// the initial parsing of the parent variable name.
				while (*s && *s != ')')
				{
					if (*s == '@') *s = '%';
					s++;
				}

				if (*s != ')')
				{
					CATERR("Missing ')' for '%s'\n", var);
					goto fail;
				}

				*s = 0;

				if (!(rel_to_path = strdup(start)))
				{
					CATERR("Out of memory\n");
					goto fail;
				}
			}
		}

		catcierge_free_list(path_ops_list, count);
	}

	//
	// Perform actions in a consistent order that makes sense:
	//
	the_path = !(*path->full) ? path->dir : path->full;

	if (is_dir) the_path = path->dir;
	if (is_abs) the_path = catcierge_get_abs_path(the_path, buf, bufsize);

	// If no relative path was specified, use the global
	// root path if set for the current template.
	if (!rel_to_path && template && template->settings.rootpath)
	{
		if (!(rel_to_path = strdup(template->settings.rootpath)))
		{
			CATERR("Out of memory\n");
			goto fail;
		}
	}

	if (rel_to_path && !ctx->no_relative_path)
	{
		char abs_rel_to_path[4096];
		char *full_rel_to_path = NULL;

		// Generate the full relative path.
		ctx->no_relative_path = 1;
		full_rel_to_path = catcierge_output_generate(&grb->output, grb, rel_to_path);
		ctx->no_relative_path = 0;

		// The relativeness must be based on absolute paths.
		the_path = catcierge_get_abs_path(the_path, buf, bufsize);
		catcierge_get_abs_path(full_rel_to_path,
							abs_rel_to_path, sizeof(abs_rel_to_path));

		// Relative path (the_path is allocated).
		the_path = catcierge_relative_path(abs_rel_to_path, the_path);
		free(full_rel_to_path);
	}

	snprintf(buf, bufsize - 1, "%s", the_path);

	if (rel_to_path && !ctx->no_relative_path)
	{
		free(the_path);
		free(rel_to_path);
	}

	return buf;

fail:
	catcierge_free_list(path_ops_list, count);
	return NULL;
}

#define DIR_ONLY 1
//
// Helper function to create a path struct from a path string
// and then call catcierge_get_path on it.
//
char *catcierge_create_and_get_path(catcierge_grb_t *grb, const char *var,
								const char *path_val, int dir_only,
								char *buf, size_t bufsize)
{
	char *dir = NULL;
	char *dir_end = NULL;
	char *filename = NULL;
	char *path_copy = NULL;
	catcierge_path_t path;

	if (!path_val)
		return NULL;

	memset(&path, 0, sizeof(path));

	if (!(path_copy = strdup(path_val)))
	{
		return NULL;
	}

	if (!dir_only)
	{
		char *dir_end = strrchr(path_copy, catcierge_path_sep()[0]);

		if (dir_end)
		{
			filename = dir_end + 1;
			*dir_end = 0;
		}
	}

	dir = path_copy;

	if (!dir) dir = "./";
	if (!dir_end) dir_end = dir + strlen(dir);

	snprintf(path.dir, sizeof(path.dir), "%s%s",
			dir, (*(dir_end - 1) == '/') ? "" : "");

	if (filename)
	{
		snprintf(path.filename, sizeof(path.filename), "%s", filename);
	}

	free(path_copy);

	return catcierge_get_path(grb, var, &path, buf, bufsize);
} 

const char *catcierge_output_translate(catcierge_grb_t *grb,
	char *buf, size_t bufsize, char *var)
{
	const char *matcher_val;
	match_group_t *mg = &grb->match_group;

	if (!strncmp(var, "template_path", 13))
	{
		char *template_path = catcierge_get_template_path(grb, var);
		return catcierge_create_and_get_path(grb, var,
					template_path, 0, buf, bufsize);
	}

	// Current time.
	if (!strncmp(var, "time", 4))
	{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		return catcierge_get_time_var_format(var, buf, bufsize,
			"%Y-%m-%d %H:%M:%S.%f", time(NULL), &tv);
	}

	if (!strcmp(var, "state"))
	{
		return catcierge_get_state_string(grb->state);
	}

	if (!strcmp(var, "prev_state"))
	{
		return catcierge_get_state_string(grb->prev_state);
	}

	if (!strcmp(var, "git_commit") || !strcmp(var, "git_hash"))
	{
		return CATCIERGE_GIT_HASH;
	}

	if (!strcmp(var, "git_commit_short") || !strcmp(var, "git_hash_short"))
	{
		return CATCIERGE_GIT_HASH_SHORT;
	}

	if (!strcmp(var, "git_tainted"))
	{
		snprintf(buf, bufsize - 1, "%d", CATCIERGE_GIT_TAINTED);
		return buf;
	}

	if (!strcmp(var, "version"))
	{
		return CATCIERGE_VERSION_STR;
	}

	if (!strcmp(var, "cwd"))
	{
		if (!getcwd(buf, bufsize - 1))
		{
			CATERR("Failed to get cwd\n"); return NULL;
		}

		return buf;
	}

	#define CHECK_OUTPUT_PATH_VAR(name, _output) \
		if (!strcmp(name, #_output)) \
		{ \
			return catcierge_create_and_get_path(grb, var, \
						grb->args._output, DIR_ONLY, buf, bufsize); \
		}

	CHECK_OUTPUT_PATH_VAR(var, output_path);
	CHECK_OUTPUT_PATH_VAR(var, match_output_path);
	CHECK_OUTPUT_PATH_VAR(var, steps_output_path);
	CHECK_OUTPUT_PATH_VAR(var, obstruct_output_path);
	CHECK_OUTPUT_PATH_VAR(var, template_output_path);

	if (!strcmp(var, "matcher"))
	{
		return grb->matcher->short_name;
	}

	if (grb->matcher && (matcher_val = grb->matcher->translate(grb->matcher, var, buf, bufsize)))
	{
		return matcher_val;
	}

	if (!strcmp(var, "ok_matches_needed"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->args.ok_matches_needed);
		return buf;
	}

	if (!strcmp(var, "no_final_decision"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->args.no_final_decision);
		return buf;
	}

	if (!strcmp(var, "matchtime"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->args.match_time);
		return buf;
	}

	if (!strcmp(var, "lockout_method"))
	{
		snprintf(buf, bufsize - 1, "%d", (int)grb->args.lockout_method);
		return buf;
	}

	if (!strcmp(var, "lockout_error"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->args.max_consecutive_lockout_count);
		return buf;
	}

	if (!strcmp(var, "lockout_error_delay"))
	{
		snprintf(buf, bufsize - 1, "%0.2f", grb->args.consecutive_lockout_delay);
		return buf;
	}

	if (!strcmp(var, "lockout_time"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->args.lockout_time);
		return buf;
	}

	if (!strncmp(var, "match_group_id", 14))
	{
		char *subvar = var + 14;
		return catcierge_get_short_id(subvar, buf, bufsize, &mg->sha);
	}

	if (!strncmp(var, "match_group_start_time", 22))
	{
		var += strlen("match_group_start_");
		return catcierge_get_time_var_format(var, buf, bufsize,
					"%Y-%m-%d %H:%M:%S.%f", mg->start_time, &mg->start_tv);
	}

	if (!strncmp(var, "match_group_end_time", 20))
	{
		var += strlen("match_group_end_");
		return catcierge_get_time_var_format(var, buf, bufsize,
					"%Y-%m-%d %H:%M:%S.%f", mg->end_time, &mg->end_tv);
	}

	if (!strcmp(var, "match_group_success")
	 || !strcmp(var, "match_success"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->match_group.success);
		return buf;
	}

	if (!strcmp(var, "match_group_success_count"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->match_group.success_count);
		return buf;
	}

	if (!strcmp(var, "match_group_final_decision"))
	{
		snprintf(buf, bufsize - 1, "%d", grb->match_group.final_decision);
		return buf;
	}

	if (!strcmp(var, "match_group_direction"))
	{
		return catcierge_get_direction_str(grb->match_group.direction);
	}

	if (!strcmp(var, "match_group_description")
	 || !strcmp(var, "match_group_desc"))
	{
		return grb->match_group.description;
	}

	if (!strcmp(var, "match_group_count")
	 || !strcmp(var, "match_count"))
	{
		snprintf(buf, bufsize - 1, "%d", (int)grb->match_group.match_count);
		return buf;
	}

	if (!strcmp(var, "match_group_max_count"))
	{
		snprintf(buf, bufsize - 1, "%d", MATCH_MAX_COUNT);
		return buf;
	}

	if (!strcmp(var, "obstruct_filename"))
	{
		return mg->obstruct_path.filename;
	}

	if (!strncmp(var, "obstruct_path", 13))
	{
		return catcierge_get_path(grb, var, &mg->obstruct_path, buf, bufsize);
	}

	if (!strncmp(var, "obstruct_time", strlen("obstruct_time")))
	{
		char *subvar = var + strlen("obstruct_");
		return catcierge_get_time_var_format(subvar, buf, bufsize,
					"%Y-%m-%d %H:%M:%S.%f", mg->obstruct_time, &mg->obstruct_tv);
	}

	if (!strncmp(var, "match", 5))
	{
		int idx = -1;
		match_state_t *m = NULL;
		char *subvar = NULL;

		if (!strncmp(var, "matchcur", 8))
		{
			idx = grb->match_group.match_count;
			subvar = var + strlen("matchcur_");
			idx--;
		}
		else
		{
			subvar = var + strlen("match#_");

			if (sscanf(var, "match%d_", &idx) == EOF)
			{
				CATERR("Output: Failed to parse %s\n", var); return NULL;
			}

			idx--; // Convert to 0-based index.
		}

		if ((idx < 0) || (idx >= MATCH_MAX_COUNT))
		{
			CATERR("Output: %s out of index. (%lu > %lu)\n", var, idx, grb->match_group.match_count); return NULL;
		}

		m = &grb->match_group.matches[idx];

		if ((size_t)idx > grb->match_group.match_count)
		{
			CATERR("Output: %s out of index. (%lu > %lu)\n", var, idx, grb->match_group.match_count);
			return "";
		}

		if (!strncmp(subvar, "path", 4))
		{
			return catcierge_get_path(grb, var, &m->path, buf, bufsize);
		}
		else if (!strcmp(subvar, "filename"))
		{
			return m->path.filename;
		}
		else if (!strncmp(subvar, "idx", 3))
		{
			snprintf(buf, bufsize - 1, "%d", idx + 1);
			return buf;
		}
		else if (!strncmp(subvar, "id", 2))
		{
			char *subsubvar = subvar + 2;
			return catcierge_get_short_id(subsubvar, buf, bufsize, &m->sha);
		}
		else if (!strcmp(subvar, "success"))
		{
			snprintf(buf, bufsize - 1, "%d", m->result.success);
			return buf;
		}
		else if (!strcmp(subvar, "success_str"))
		{
			snprintf(buf, bufsize - 1, "%s", m->result.success ? "success": "fail");
			return buf;
		}
		else if (!strcmp(subvar, "direction"))
		{
			return catcierge_get_direction_str(m->result.direction);
		}
		else if (!strncmp(subvar, "desc", 4))
		{
			return m->result.description;
		}
		else if (!strcmp(subvar, "result"))
		{
			snprintf(buf, bufsize - 1, "%f", m->result.result);
			return buf;
		}
		else if (!strncmp(subvar, "time", 4))
		{
			return catcierge_get_time_var_format(subvar, buf, bufsize,
					"%Y-%m-%d %H:%M:%S.%f", m->time, &m->tv);
		}
		else if (!strcmp(subvar, "step_count"))
		{
			snprintf(buf, bufsize - 1, "%d", (int)m->result.step_img_count);
			return buf;
		}
		else if (!strncmp(subvar, "step", 4))
		{
			// Match step images / descriptions.
			int stepidx = -1;
			match_step_t *step = NULL;

			const char *stepvar = subvar + strlen("step#_");

			if (sscanf(subvar, "step%d_", &stepidx) == EOF)
			{
				CATERR("Failed to parse step#_\n");
				return NULL;
			}

			// "step##_" instead of just "step#_"
			if (stepidx >= 10)
				stepvar++;

			stepidx--; // Convert to 0-based index.

			if ((stepidx < 0) || (stepidx >= MAX_STEPS))
			{
				CATERR("Step index out of range %d\n", stepidx);
				return NULL;
			}

			step = &m->result.steps[stepidx];

			if (!strcmp(stepvar, "path"))
			{
				return catcierge_get_path(grb, var, &step->path, buf, bufsize);
			}
			else if (!strcmp(stepvar, "filename"))
			{
				return step->path.filename;
			}
			else if (!strcmp(stepvar, "name"))
			{
				return step->name ? step->name : "";
			}
			else if (!strncmp(stepvar, "desc", 4))
			{
				return step->description ? step->description : "";
			}
			else if (!strcmp(stepvar, "active"))
			{
				snprintf(buf, bufsize - 1, "%d", step->img != NULL);
				return buf;
			}
		}
	}

	return NULL;
}

static char *catcierge_output_realloc_if_needed(char *str, size_t new_size, size_t *max_size)
{
	assert(max_size);

	while (new_size >= *max_size)
	{
		//CATLOG("Realloc needed! %lu >= %u\n", new_size, *max_size);
		(*max_size) *= 2;

		if (!(str = realloc(str, *max_size)))
		{
			CATERR("Out of memory\n"); return NULL;
		}
	}

	return str;
}

char *catcierge_output_generate(catcierge_output_t *ctx,
	catcierge_grb_t *grb, const char *template_str)
{
	char buf[4096];
	char *s;
	char *it;
	char *output = NULL;
	char *tmp = NULL;
	size_t orig_len = 0;
	size_t out_len = 2 * orig_len + 1;
	size_t len;
	size_t linenum;
	size_t reslen;
	assert(ctx);
	assert(grb);

	if (!template_str)
		return NULL;

	orig_len = strlen(template_str);

	if (ctx->recursion >= CATCIERGE_OUTPUT_MAX_RECURSION)
	{
		CATERR("Max output template recursion level reached (%d)!\n",
			CATCIERGE_OUTPUT_MAX_RECURSION);
		ctx->recursion_error = 1;
		return NULL;
	}

	if (!(output = malloc(out_len)))
	{
		return NULL;
	}

	if (!(tmp = strdup(template_str)))
	{
		free(output);
		return NULL;
	}

	len = 0;
	linenum = 0;
	it = tmp;

	while (*it)
	{
		if (*it == '\n')
		{
			linenum++;
		}

		// Replace any variables signified by %varname%
		if (*it == '%')
		{
			const char *res;
			it++;

			// %% means a literal %
			if (*it && (*it == '%'))
			{
				output[len] = *it++;
				len++;
				continue;
			}

			// Save position of beginning of var name.
			s = it;

			// Look for the ending %
			while (*it && (*it != '%') && (*it != '\n'))
			{
				it++;
			}

			// Either we found it or the end of string.
			if (*it != '%')
			{
				*it = '\0';
				CATERR("Variable \"%s\" not terminated in output template line %d\n",
					s, (int)linenum);
				free(output);
				output = NULL;
				goto fail;
			}

			// Terminate so we get the var name in a nice comparable string.
			*it++ = '\0';

			// Some variables can nest other variables, make sure
			// we don't end up in an infinite recursion.
			ctx->recursion++;

			// Find the value of the variable and append it to the output.
			if (!(res = catcierge_output_translate(grb, buf, sizeof(buf), s)))
			{
				if (ctx->recursion_error)
				{
					CATERR(" %*s\"%s\"\n", (CATCIERGE_OUTPUT_MAX_RECURSION - ctx->recursion), "", s);
					ctx->recursion--;

					if (ctx->recursion == 0)
						ctx->recursion_error = 0;
				}
				else
				{
					CATERR("Unknown template variable \"%s\"\n", s);
				}

				free(output);
				output = NULL;
				goto fail;
			}

			ctx->recursion--;

			reslen = strlen(res);

			// Make sure we have enough room.
			if (!(output = catcierge_output_realloc_if_needed(output, (len + reslen + 1), &out_len)))
			{
				goto fail;
			}

			// Append the variable to the output.
			while (*res)
			{
				output[len] = *res++;
				len++;
			}
		}
		else
		{
			// Make sure we have enough room.
			if (!(output = catcierge_output_realloc_if_needed(output, (len + 1), &out_len)))
			{
				goto fail;
			}

			output[len] = *it++;
			len++;
		}
	}

	output[len] = '\0';

fail:
	if (tmp)
		free(tmp);

	return output;
}

int catcierge_output_validate(catcierge_output_t *ctx,
	catcierge_grb_t *grb, const char *template_str)
{
	int is_valid = 0;
	char *output = catcierge_output_generate(ctx, grb, template_str);
	is_valid = (output != NULL);

	if (output)
		free(output);

	return is_valid;
}

static char *catcierge_replace_whitespace(char *path, char *extra_chars)
{
	char *p = path;
	char *e = NULL;

	while (*p)
	{
		if ((*p == ' ') || (*p == '\t') || (*p == '\n'))
		{
			*p = '_';
		}

		if (extra_chars)
		{
			e = extra_chars;
			while (*e)
			{
				if (*p == *e)
				{
					*p = '_';
				}

				e++;
			}
		}

		p++;
	}

	return 0;
}

static void catcierge_output_free_generated_paths(catcierge_output_t *ctx)
{
	size_t i;
	catcierge_output_template_t *t = NULL;
	assert(ctx);

	for (i = 0; i < ctx->template_count; i++)
	{
		t = &ctx->templates[i];

		if (t->generated_path)
		{
			free(t->generated_path);
			t->generated_path = NULL;
		}
	}
}

int catcierge_output_template_registered_to_event(catcierge_output_template_t *t, const char *event)
{
	size_t i;
	assert(t);
	assert(event);

	for (i = 0; i < t->settings.event_filter_count; i++)
	{
		if (!strcmp(t->settings.event_filter[i], "all")
		 || !strcmp(t->settings.event_filter[i], "*"))
		{
			return 1;
		}

		if (!strcmp(t->settings.event_filter[i], event))
		{
			return 1;
		}
	}

	return 0;
}

int catcierge_output_generate_templates(catcierge_output_t *ctx,
	catcierge_grb_t *grb, const char *event)
{
	catcierge_output_template_t *t = NULL;
	catcierge_args_t *args = &grb->args;
	char *output = NULL;
	char *path = NULL;
	char full_path[4096];
	char *dir = NULL;
	char *gen_output_path = NULL;
	size_t i;
	FILE *f = NULL;
	assert(ctx);
	assert(grb);

	if (!args->template_output_path && args->output_path)
	{
		if (!(args->template_output_path = strdup(args->output_path)))
		{
			CATERR("Out of memory");
			return -1;
		}
	}

	catcierge_output_free_generated_paths(ctx);

	for (i = 0; i < ctx->template_count; i++)
	{
		ctx->template_idx = i;
		t = &ctx->templates[i];

		// Filter out any events that don't have the current "event" in their list.
		if (!catcierge_output_template_registered_to_event(t, event))
		{
			//CATLOG("  Skip template %s because event %s not registered for it\n", t->name, event);
			continue;
		}

		// First generate the target path
		// (It is important this comes first, since we might refer to the generated
		// path later, either inside the template itself, but most importantly we
		// want to be able to pass the path to an external program).
		if (args->template_output_path)
		{
			// Generate the output path.
			if (!(gen_output_path = catcierge_output_generate(&grb->output,
					grb, args->template_output_path)))
			{
				CATERR("Failed to generate output path from: \"%s\"\n", args->template_output_path);
				goto fail_template;
			}

			if (catcierge_make_path(gen_output_path))
			{
				CATERR("Failed to create directory %s\n", gen_output_path);
			}

			// Generate the filename.
			if (!(path = catcierge_output_generate(ctx, grb, t->settings.filename)))
			{
				CATERR("Failed to generate output path for template \"%s\"\n", t->settings.filename);
				goto fail_template;
			}

			// Replace whitespace with underscore.
			catcierge_replace_whitespace(path, ":");

			// Assemble the full output path.
			snprintf(full_path, sizeof(full_path), "%s%s%s",
				gen_output_path, catcierge_path_sep(), path);

			// We make a copy so that we can use the generated
			// path as a variable in the templates contents, or
			// when passed to catcierge_execute. 
			if (!(t->generated_path = strdup(full_path)))
			{
				CATERR("Out of memory!\n");
				goto fail_template;
			}
		}

		// And then generate the template contents.
		if (!(output = catcierge_output_generate(ctx, grb, t->tmpl)))
		{
			CATERR("Failed to generate output for template \"%s\"\n", t->settings.filename);
			goto fail_template;
		}

		#ifdef WITH_ZMQ
		if (grb->args.zmq && grb->zmq_pub && !t->settings.nozmq)
		{
			CATLOG("ZMQ Publish topic %s, %d bytes\n", t->settings.topic, strlen(output));
			zstr_sendfm(grb->zmq_pub, t->settings.topic);
			zstr_send(grb->zmq_pub, output);
		}
		#endif // WITH_ZMQ

		if (!t->settings.nofile)
		{
			CATLOG("Generate template: %s\n", full_path);

			if (!(f = fopen(full_path, "w")))
			{
				CATERR("Failed to open template output file \"%s\" for writing\n", full_path);
				goto fail_template;
			}
			else
			{
				size_t len = strlen(output);
				size_t written = fwrite(output, sizeof(char), len, f);
				fclose(f);
			}
		}

fail_template:
		if (output)
		{
			free(output);
			output = NULL;
		}

		if (path)
		{
			free(path);
			path = NULL;
		}

		if (gen_output_path)
		{
			free(gen_output_path);
			gen_output_path = NULL;
		}
	}

	ctx->template_idx = -1;

	return 0;
}

int catcierge_output_load_template(catcierge_output_t *ctx, char *path)
{
	int ret = 0;
	size_t fsize;
	size_t read_bytes;
	FILE *f = NULL;
	char *contents = NULL;
	char *settings_end = NULL;
	assert(path);
	assert(ctx);

	if (!(f = fopen(path, "r")))
	{
		CATERR("Failed to open input template file \"%s\"\n", path);
		ret = -1; goto fail;
	}

	// Get file size.
	if (fseek(f, 0, SEEK_END))
	{
		CATERR("Failed to seek in template file \"%s\"\n", path);
		ret = -1; goto fail;
	}

	if ((fsize = ftell(f)) == -1)
	{
		CATERR("Failed to get file size for template file \"%s\"\n", path);
		ret = -1; goto fail;
	}

	rewind(f);

	// Make sure we allocate enough to fit a NULL
	// character at the end of the file contents.
	if (!(contents = calloc(1, fsize + 1)))
	{
		CATERR("Out of memory!\n");
		ret = -1; goto fail;
	}

	read_bytes = fread(contents, 1, fsize, f);
	contents[read_bytes] = '\0';

	#ifndef _WIN32
	if (read_bytes != fsize)
	{
		CATERR("Failed to read file contents of template file \"%s\". "
				"Got %d expected %d\n", path, (int)read_bytes, (int)fsize);
		ret = -1; goto fail;
	}
	#endif // !_WIN32

	if (catcierge_output_add_template(ctx, contents, path))
	{
		CATERR("Failed to load template file \"%s\"\n", path);
		ret = -1; goto fail;
	}

fail:
	if (contents)
	{
		free(contents);
		contents = NULL;
	}

	if (f)
	{
		fclose(f);
	}

	return ret;
}

int catcierge_output_load_templates(catcierge_output_t *ctx,
		char **inputs, size_t input_count)
{
	int ret = 0;
	size_t i;

	if (input_count > 0)
	{
		CATLOG("Loading output templates:\n");
	}

	for (i = 0; i < input_count; i++)
	{
		if (catcierge_output_load_template(ctx, inputs[i]))
		{
			return -1;
		}
	}

	return ret;
}

void catcierge_output_execute_list(catcierge_grb_t *grb,
		const char *event, char **commands, size_t command_count)
{
	size_t i;

	if (catcierge_output_generate_templates(&grb->output, grb, event))
	{
		CATERR("Failed to generate templates on execute!\n");
		return;
	}

	for (i = 0; i < command_count; i++)
	{
		catcierge_output_execute(grb, event, commands[i]);
	}
}

void catcierge_output_execute(catcierge_grb_t *grb,
		const char *event, const char *command)
{
	char *generated_cmd = NULL;

	if (!command)
	{
		return;
	}

	if (!(generated_cmd = catcierge_output_generate(&grb->output, grb, command)))
	{
		CATERR("Failed to execute command \"%s\"!\n", command);
		return;
	}

	catcierge_run(generated_cmd);

	free(generated_cmd);
}


