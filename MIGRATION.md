# Migrating from utils.h to kit.h

The library took a name and a namespace. Every public identifier moved under
`kit_`, `Kit` or `KIT_`, and most modules got their own vocabulary in the move.
Behaviour did not change: this is a rename, and every entry below does exactly
what its old name did.

## What changed beyond the prefix

- **The header is `kit.h`**, and the implementation macro is
  `KIT_IMPLEMENTATION`.
- **Module words replaced abbreviations**: `sv` became `str`, `sb` became
  `buf`, `da` became `array`, `hm` became `map`, `temp` became `scratch`,
  `opts` became `cli`, `cmd` became `command`, and the file functions became
  `fs`.
- **Some names now say what the function does.** `file_exists` only ever
  accepted regular files, so it is `kit_fs_is_file`. `needs_rebuild` is
  `kit_fs_stale`, `sv_chop_by_delim` is `kit_str_cut`, and the loop form
  `sv_try_chop_by_delim` is `kit_str_next`.
- **`return_defer` is `KIT_BAIL`**, and the label it jumps to is `cleanup:`
  instead of `defer:`.
- **`LOG_WARNING` is `KIT_LOG_WARN`**, matching the label the logger prints.
- **`LOG(LOG_INFO, ...)` is best written `KIT_INFO(...)`.** There is one macro
  per level: `KIT_DEBUG`, `KIT_INFO`, `KIT_WARN`, `KIT_ERROR` and
  `KIT_CRITICAL`. `KIT_LOG(level, ...)` remains for a level chosen at run
  time.

## Since the rename

**The filesystem reports failures through `KitError`.** Every `kit_fs_*`
function that can fail takes a `KitError *` as its last argument. Passing
`NULL` keeps the old behaviour, the failure is logged, so the smallest possible
migration is to append `, NULL` to each call. `kit_fs_is_file`, `kit_fs_is_dir`
and `kit_fs_kind` answer a question rather than fail, and are unchanged.

```c
char *text = kit_fs_read("config.ini");              /* before */
char *text = kit_fs_read("config.ini", NULL);        /* same behaviour */

KitError err = KIT_ZEROED;                            /* or take the failure */
char *text = kit_fs_read("config.ini", &err);
if (!text && err.code == KIT_ERR_NOT_FOUND) { ... }
```

**Processes and option parsing report failures the same way.**
`kit_command_run`, `kit_command_spawn`, `kit_process_wait`, the three
`kit_command_capture*` functions and `kit_cli_parse` all take a `KitError *`
last, and `kit_cli_parse_arr` gained the argument too. `kit_command_run_args`
is the exception: varargs must come last, so it always logs.

```c
if (!kit_cli_parse_arr(opts, &argc, &argv))        /* before */
if (!kit_cli_parse_arr(opts, &argc, &argv, NULL))  /* same behaviour */
```

A command that cannot be started is now reported with the reason the system
gave, `KIT_ERR_NOT_FOUND` for a missing program, instead of the child exiting
with code 127. A command that ran and failed is `KIT_ERR_PROCESS`, with its
exit status or signal number in `native`.

## Every name

### Configuration

| utils.h | kit.h |
|---|---|
| `UTILS_IMPLEMENTATION` | `KIT_IMPLEMENTATION` |
| `UTILS_NO_VEC_MATH` | `KIT_NO_VEC_MATH` |
| `UTILS_NO_THREAD_LOCAL` | `KIT_NO_THREAD_LOCAL` |
| `UTILS_READ_CHUNK` | `KIT_READ_CHUNK` |
| `DA_INIT_CAP` | `KIT_ARRAY_INIT_CAP` |
| `ARENA_REGION_SIZE` | `KIT_ARENA_REGION_SIZE` |

### Language helpers

| utils.h | kit.h |
|---|---|
| `UTILS_ARRAY_LEN` | `KIT_COUNTOF` |
| `UTILS_UNUSED` | `KIT_UNUSED` |
| `UTILS_ZEROED` | `KIT_ZEROED` |
| `UTILS_LITERAL` | `KIT_LITERAL` |
| `UTILS_CAST_LIKE` | `KIT_CAST_LIKE` |
| `UTILS_NORETURN` | `KIT_NORETURN` |
| `UTILS_PRINTF_FORMAT` | `KIT_PRINTF_FORMAT` |
| `UTILS_THREAD_LOCAL` | `KIT_THREAD_LOCAL` |
| `return_defer` | `KIT_BAIL` |
| `TODO` | `KIT_TODO` |
| `UNREACHABLE` | `KIT_UNREACHABLE` |
| `PANIC` | `KIT_PANIC` |

### Logging

| utils.h | kit.h |
|---|---|
| `LogLevel` | `KitLogLevel` |
| `LOG_DEBUG` | `KIT_LOG_DEBUG` |
| `LOG_INFO` | `KIT_LOG_INFO` |
| `LOG_WARNING` | `KIT_LOG_WARN` |
| `LOG_ERROR` | `KIT_LOG_ERROR` |
| `LOG_CRITICAL` | `KIT_LOG_CRITICAL` |
| `LogColorMode` | `KitLogColor` |
| `LOG_COLOR_AUTO` | `KIT_LOG_COLOR_AUTO` |
| `LOG_COLOR_ALWAYS` | `KIT_LOG_COLOR_ALWAYS` |
| `LOG_COLOR_NEVER` | `KIT_LOG_COLOR_NEVER` |
| `LogField` | `KitLogField` |
| `LOG_FIELD_TIME` | `KIT_LOG_FIELD_TIME` |
| `LOG_FIELD_DATE` | `KIT_LOG_FIELD_DATE` |
| `LOG_FIELD_LEVEL` | `KIT_LOG_FIELD_LEVEL` |
| `LOG_FIELD_LOCATION` | `KIT_LOG_FIELD_LOCATION` |
| `LOG_FIELD_USER` | `KIT_LOG_FIELD_USER` |
| `LOG_FIELD_COUNT` | `KIT_LOG_FIELD_COUNT` |
| `LOG_FIELDS_DEFAULT` | `KIT_LOG_FIELDS_DEFAULT` |
| `LOG_FIELDS_ALL` | `KIT_LOG_FIELDS_ALL` |
| `LOG_FIELDS_NONE` | `KIT_LOG_FIELDS_NONE` |
| `LOG` | `KIT_LOG` |
| `log_set_level` | `kit_log_set_level` |
| `log_set_output` | `kit_log_set_output` |
| `log_set_color` | `kit_log_set_color` |
| `log_set_fields` | `kit_log_set_fields` |
| `utils_log_impl` | `kit__log` |
| `utils_panic_impl` | `kit__panic` |

### Filesystem

| utils.h | kit.h |
|---|---|
| `read_file` | `kit_fs_read` |
| `read_file_ex` | `kit_fs_read_sized` |
| `write_file` | `kit_fs_write` |
| `file_exists` | `kit_fs_is_file` |
| `dir_exists` | `kit_fs_is_dir` |
| `file_size` | `kit_fs_size` |
| `file_mtime` | `kit_fs_mtime` |
| `FileKind` | `KitFileKind` |
| `FILE_KIND_NONE` | `KIT_FILE_KIND_NONE` |
| `FILE_KIND_REGULAR` | `KIT_FILE_KIND_REGULAR` |
| `FILE_KIND_DIRECTORY` | `KIT_FILE_KIND_DIRECTORY` |
| `FILE_KIND_OTHER` | `KIT_FILE_KIND_OTHER` |
| `file_kind` | `kit_fs_kind` |
| `mkdir_p` | `kit_fs_mkdir` |
| `copy_file` | `kit_fs_copy` |
| `remove_file` | `kit_fs_remove` |
| `remove_dir` | `kit_fs_rmdir` |
| `rename_file` | `kit_fs_rename` |
| `FileList` | `KitFileList` |
| `file_list_free` | `kit_file_list_free` |
| `read_dir` | `kit_fs_list` |
| `needs_rebuild` | `kit_fs_stale` |
| `needs_rebuild_list` | `kit_fs_stale_list` |
| `needs_rebuild1` | `kit_fs_stale1` |

### Dynamic arrays

| utils.h | kit.h |
|---|---|
| `da_reserve` | `kit_array_reserve` |
| `da_append` | `kit_array_push` |
| `da_append_many` | `kit_array_push_many` |
| `da_pop` | `kit_array_pop` |
| `da_first` | `kit_array_first` |
| `da_last` | `kit_array_last` |
| `da_remove_unordered` | `kit_array_swap_remove` |
| `da_foreach` | `kit_array_each` |
| `da_index_of` | `kit_array_find` |
| `da_contains` | `kit_array_contains` |
| `da_free` | `kit_array_free` |

### Strings

| utils.h | kit.h |
|---|---|
| `String_View` | `KitStr` |
| `SV` | `KIT_STR` |
| `SV_LIT` | `KIT_STR_LIT` |
| `SV_Fmt` | `KIT_STR_FMT` |
| `SV_Arg` | `KIT_STR_ARG` |
| `SV_NPOS` | `KIT_NPOS` |
| `sv_from_cstr` | `kit_str_from` |
| `sv_from_parts` | `kit_str_from_parts` |
| `sv_trim` | `kit_str_trim` |
| `sv_trim_left` | `kit_str_trim_left` |
| `sv_trim_right` | `kit_str_trim_right` |
| `sv_chop_by_delim` | `kit_str_cut` |
| `sv_chop_by_sv` | `kit_str_cut_str` |
| `sv_try_chop_by_delim` | `kit_str_next` |
| `sv_chop_left` | `kit_str_take` |
| `sv_chop_right` | `kit_str_take_right` |
| `sv_eq` | `kit_str_eq` |
| `sv_eq_cstr` | `kit_str_eq_cstr` |
| `sv_eq_ignorecase` | `kit_str_eq_nocase` |
| `sv_starts_with` | `kit_str_starts_with` |
| `sv_starts_with_cstr` | `kit_str_starts_with_cstr` |
| `sv_ends_with` | `kit_str_ends_with` |
| `sv_ends_with_cstr` | `kit_str_ends_with_cstr` |
| `sv_index_of` | `kit_str_find_char` |
| `sv_index_of_sv` | `kit_str_find` |
| `sv_contains` | `kit_str_contains` |
| `sv_to_i64` | `kit_str_to_i64` |
| `sv_to_u64` | `kit_str_to_u64` |
| `sv_to_double` | `kit_str_to_double` |
| `sv_to_cstr` | `kit_str_dup` |

### String builder

| utils.h | kit.h |
|---|---|
| `StringBuilder` | `KitBuf` |
| `sb_append` | `kit_buf_append` |
| `sb_append_n` | `kit_buf_append_n` |
| `sb_append_char` | `kit_buf_append_char` |
| `sb_append_sv` | `kit_buf_append_str` |
| `sb_appendf` | `kit_buf_printf` |
| `sb_cstr` | `kit_buf_cstr` |
| `sb_to_string` | `kit_buf_dup` |
| `sb_reset` | `kit_buf_reset` |
| `sb_free` | `kit_buf_free` |

### Arena and scratch memory

| utils.h | kit.h |
|---|---|
| `Arena` | `KitArena` |
| `Arena_Region` | `KitArenaRegion` |
| `Arena_Mark` | `KitArenaMark` |
| `arena_make` | `kit_arena_make` |
| `arena_alloc` | `kit_arena_alloc` |
| `arena_alloc_aligned` | `kit_arena_alloc_aligned` |
| `arena_alloc_array` | `kit_arena_alloc_array` |
| `arena_strdup` | `kit_arena_strdup` |
| `arena_strdup_n` | `kit_arena_strndup` |
| `arena_sprintf` | `kit_arena_printf` |
| `arena_used` | `kit_arena_used` |
| `arena_capacity` | `kit_arena_capacity` |
| `arena_reset` | `kit_arena_reset` |
| `arena_free` | `kit_arena_free` |
| `arena_mark` | `kit_arena_mark` |
| `arena_rewind` | `kit_arena_rewind` |
| `temp_alloc` | `kit_scratch_alloc` |
| `temp_strdup` | `kit_scratch_strdup` |
| `temp_sprintf` | `kit_scratch_printf` |
| `temp_mark` | `kit_scratch_mark` |
| `temp_rewind` | `kit_scratch_rewind` |
| `temp_reset` | `kit_scratch_reset` |
| `temp_free` | `kit_scratch_free` |

### Timing

| utils.h | kit.h |
|---|---|
| `Stopwatch` | `KitTimer` |
| `sw_start` | `kit_timer_start` |
| `sw_elapsed_s` | `kit_timer_s` |
| `sw_elapsed_ms` | `kit_timer_ms` |

### Command line

| utils.h | kit.h |
|---|---|
| `args_shift` | `kit_cli_shift` |
| `Opt` | `KitCliOpt` |
| `OptType` | `KitCliOptType` |
| `OPTTYPE_FLAG` | `KIT_CLI_OPT_FLAG` |
| `OPTTYPE_STR` | `KIT_CLI_OPT_STR` |
| `OPTTYPE_INT` | `KIT_CLI_OPT_INT` |
| `OPT_FLAG` | `KIT_CLI_FLAG` |
| `OPT_STR` | `KIT_CLI_STR` |
| `OPT_INT` | `KIT_CLI_INT` |
| `opts_parse` | `kit_cli_parse` |
| `opts_usage` | `kit_cli_usage` |
| `opts_parse_arr` | `kit_cli_parse_arr` |
| `opts_usage_arr` | `kit_cli_usage_arr` |

### Processes

| utils.h | kit.h |
|---|---|
| `Cmd` | `KitCommand` |
| `cmd_append` | `kit_command_push` |
| `cmd_extend` | `kit_command_push_all` |
| `cmd_reset` | `kit_command_reset` |
| `cmd_free` | `kit_command_free` |
| `cmd_run` | `kit_command_run` |
| `cmd_run_args` | `kit_command_run_args` |
| `cmd_run_async` | `kit_command_spawn` |
| `cmd_capture` | `kit_command_capture` |
| `cmd_capture_merged` | `kit_command_capture_merged` |
| `cmd_capture_ex` | `kit_command_capture_split` |
| `Proc` | `KitProcess` |
| `INVALID_PROC` | `KIT_PROCESS_INVALID` |
| `proc_wait` | `kit_process_wait` |

### Hashing and hash map

| utils.h | kit.h |
|---|---|
| `hash_str` | `kit_hash_str` |
| `hash_bytes` | `kit_hash_bytes` |
| `hash_mix32` | `kit_hash_mix32` |
| `HashMap` | `KitMap` |
| `HM_Entry` | `KitMapEntry` |
| `hm_set` | `kit_map_set` |
| `hm_get` | `kit_map_get` |
| `hm_has` | `kit_map_has` |
| `hm_delete` | `kit_map_delete` |
| `hm_reset` | `kit_map_reset` |
| `hm_free` | `kit_map_free` |
| `hm_entry_live` | `kit_map_entry_live` |
| `hm_foreach` | `kit_map_each` |

### Paths

| utils.h | kit.h |
|---|---|
| `PATH_SEP` | `KIT_PATH_SEP` |
| `path_basename` | `kit_path_basename` |
| `path_ext` | `kit_path_ext` |
| `path_dirname` | `kit_path_dirname` |
| `path_join` | `kit_path_join` |
| `path_is_absolute` | `kit_path_is_absolute` |

### Maths

| utils.h | kit.h |
|---|---|
| `Vec2` | `KitVec2` |
| `Vec3` | `KitVec3` |
| `V2` | `KIT_VEC2` |
| `V3` | `KIT_VEC3` |
| `V2_Fmt` | `KIT_VEC2_FMT` |
| `V2_Arg` | `KIT_VEC2_ARG` |
| `V3_Fmt` | `KIT_VEC3_FMT` |
| `V3_Arg` | `KIT_VEC3_ARG` |
| `vec2_add` | `kit_vec2_add` |
| `vec2_sub` | `kit_vec2_sub` |
| `vec2_scale` | `kit_vec2_scale` |
| `vec2_mul` | `kit_vec2_mul` |
| `vec2_len` | `kit_vec2_len` |
| `vec2_dist` | `kit_vec2_dist` |
| `vec2_norm` | `kit_vec2_norm` |
| `vec2_dot` | `kit_vec2_dot` |
| `vec3_add` | `kit_vec3_add` |
| `vec3_sub` | `kit_vec3_sub` |
| `vec3_scale` | `kit_vec3_scale` |
| `vec3_mul` | `kit_vec3_mul` |
| `vec3_len` | `kit_vec3_len` |
| `vec3_norm` | `kit_vec3_norm` |
| `vec3_dot` | `kit_vec3_dot` |
| `vec3_cross` | `kit_vec3_cross` |
| `UTILS_MIN` | `KIT_MIN` |
| `UTILS_MAX` | `KIT_MAX` |
| `clampf` | `kit_clampf` |
| `clampd` | `kit_clampd` |
| `clampi` | `kit_clampi` |
| `lerpf` | `kit_lerpf` |
| `map_range` | `kit_remapf` |
| `DEG2RAD` | `KIT_DEG2RAD` |
| `RAD2DEG` | `KIT_RAD2DEG` |

## Doing it mechanically

Every entry above is a whole-identifier substitution, so a word-boundary
replace over your sources does the job. Two things need a human afterwards:
the `defer:` labels that `KIT_BAIL` now expects as `cleanup:`, and any string
literal that happened to contain an old name.
