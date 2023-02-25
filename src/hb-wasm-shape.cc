/*
 * Copyright © 2011  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Behdad Esfahbod
 */

#define HB_DEBUG_WASM 1

#include "hb-shaper-impl.hh"

#ifdef HAVE_WASM

#include "hb-wasm-api.hh"
#include "hb-wasm-api-list.hh"


/*
 * shaper face data
 */

#define HB_WASM_TAG_WASM HB_TAG('W','a','s','m')

struct hb_wasm_shape_plan_t {
  wasm_module_inst_t module_inst;
  wasm_exec_env_t exec_env;
  ptr_d(void, wasm_shape_plan);
};

struct hb_wasm_face_data_t {
  hb_blob_t *wasm_blob;
  wasm_module_t wasm_module;
  mutable hb_atomic_ptr_t<hb_wasm_shape_plan_t> plan;
};

static bool
init_wasm ()
{
  static bool initialized;
  if (initialized)
    return true;

  RuntimeInitArgs init_args;
  memset (&init_args, 0, sizeof (RuntimeInitArgs));

  init_args.mem_alloc_type = Alloc_With_Allocator;
  init_args.mem_alloc_option.allocator.malloc_func = (void *) hb_malloc;
  init_args.mem_alloc_option.allocator.realloc_func = (void *) hb_realloc;
  init_args.mem_alloc_option.allocator.free_func = (void *) hb_free;

  init_args.mem_alloc_type = Alloc_With_System_Allocator;

  // Native symbols need below registration phase
  init_args.n_native_symbols = ARRAY_LENGTH (_hb_wasm_native_symbols);
  init_args.native_module_name = "env";
  init_args.native_symbols = _hb_wasm_native_symbols;

  if (unlikely (!wasm_runtime_full_init (&init_args)))
  {
    DEBUG_MSG (WASM, nullptr, "Init runtime environment failed.");
    return false;
  }

  initialized = true;
  return true;
}

hb_wasm_face_data_t *
_hb_wasm_shaper_face_data_create (hb_face_t *face)
{
  hb_wasm_face_data_t *data = nullptr;
  hb_blob_t *wasm_blob = nullptr;
  wasm_module_t wasm_module = nullptr;

  wasm_blob = hb_face_reference_table (face, HB_WASM_TAG_WASM);
  unsigned length = hb_blob_get_length (wasm_blob);
  if (!length)
    goto fail;

  if (!init_wasm ())
    goto fail;

  wasm_module = wasm_runtime_load ((uint8_t *) hb_blob_get_data_writable (wasm_blob, nullptr),
				   length, nullptr, 0);
  if (unlikely (!wasm_module))
  {
    DEBUG_MSG (WASM, nullptr, "Load wasm module failed.");
    goto fail;
  }

  data = (hb_wasm_face_data_t *) hb_calloc (1, sizeof (hb_wasm_face_data_t));
  if (unlikely (!data))
    goto fail;

  data->wasm_blob = wasm_blob;
  data->wasm_module = wasm_module;

  return data;

fail:
  if (wasm_module)
      wasm_runtime_unload (wasm_module);
  hb_blob_destroy (wasm_blob);
  hb_free (data);
  return nullptr;
}

static hb_wasm_shape_plan_t *
acquire_shape_plan (hb_face_t *face,
		    const hb_wasm_face_data_t *face_data)
{
  constexpr uint32_t stack_size = 32 * 1024, heap_size = 2 * 1024 * 1024;

  wasm_module_inst_t module_inst = nullptr;
  wasm_exec_env_t exec_env = nullptr;
  wasm_function_inst_t func = nullptr;

  /* Fetch cached one if available. */
  hb_wasm_shape_plan_t *plan = face_data->plan.get_acquire ();
  if (likely (plan && face_data->plan.cmpexch (plan, nullptr)))
    return plan;

  plan = (hb_wasm_shape_plan_t *) hb_calloc (1, sizeof (hb_wasm_shape_plan_t));

  module_inst = plan->module_inst = wasm_runtime_instantiate(face_data->wasm_module,
							     stack_size, heap_size,
							     nullptr, 0);
  if (unlikely (!module_inst))
  {
    DEBUG_MSG (WASM, face_data, "Create wasm module instance failed.");
    goto fail;
  }

  exec_env = plan->exec_env = wasm_runtime_create_exec_env (module_inst,
							    stack_size);
  if (unlikely (!exec_env)) {
    DEBUG_MSG (WASM, face_data, "Create wasm execution environment failed.");
    goto fail;
  }

  func = wasm_runtime_lookup_function (module_inst, "shape_plan_create", nullptr);
  if (func)
  {
    wasm_val_t results[1];
    wasm_val_t arguments[1];

    HB_OBJ2REF (face);
    if (unlikely (!faceref))
    {
      DEBUG_MSG (WASM, face_data, "Failed to register face object.");
      goto fail;
    }

    results[0].kind = WASM_I32;
    arguments[0].kind = WASM_I32;
    arguments[0].of.i32 = faceref;
    bool ret = wasm_runtime_call_wasm_a (exec_env, func,
					 ARRAY_LENGTH (results), results,
					 ARRAY_LENGTH (arguments), arguments);

    if (unlikely (!ret))
    {
      DEBUG_MSG (WASM, module_inst, "Calling shape_plan_create() failed: %s",
		 wasm_runtime_get_exception(module_inst));
      goto fail;
    }
    plan->wasm_shape_planptr = results[0].of.i32;
  }

  return plan;

fail:

  if (exec_env)
    wasm_runtime_destroy_exec_env (exec_env);
  if (module_inst)
    wasm_runtime_deinstantiate (module_inst);
  hb_free (plan);
  return nullptr;
}

static void
release_shape_plan (const hb_wasm_face_data_t *face_data,
		    hb_wasm_shape_plan_t *plan,
		    bool cache = false)
{
  if (cache && face_data->plan.cmpexch (nullptr, plan))
    return;

  auto *module_inst = plan->module_inst;
  auto *exec_env = plan->exec_env;

  /* Is there even any point to having a shape_plan_destroy function
   * and calling it? */
  if (plan->wasm_shape_planptr)
  {

    auto *func = wasm_runtime_lookup_function (module_inst, "shape_plan_destroy", nullptr);
    if (func)
    {
      wasm_val_t arguments[1];

      arguments[0].kind = WASM_I32;
      arguments[0].of.i32 = plan->wasm_shape_planptr;
      bool ret = wasm_runtime_call_wasm_a (exec_env, func,
					   0, nullptr,
					   ARRAY_LENGTH (arguments), arguments);

      if (unlikely (!ret))
      {
	DEBUG_MSG (WASM, module_inst, "Calling shape_plan_destroy() failed: %s",
		   wasm_runtime_get_exception(module_inst));
      }
    }
  }

  wasm_runtime_destroy_exec_env (exec_env);
  wasm_runtime_deinstantiate (module_inst);
  hb_free (plan);
}

void
_hb_wasm_shaper_face_data_destroy (hb_wasm_face_data_t *data)
{
  if (data->plan.get_relaxed ())
    release_shape_plan (data, data->plan);
  wasm_runtime_unload (data->wasm_module);
  hb_blob_destroy (data->wasm_blob);
  hb_free (data);
}


/*
 * shaper font data
 */

struct hb_wasm_font_data_t {};

hb_wasm_font_data_t *
_hb_wasm_shaper_font_data_create (hb_font_t *font HB_UNUSED)
{
  return (hb_wasm_font_data_t *) HB_SHAPER_DATA_SUCCEEDED;
}

void
_hb_wasm_shaper_font_data_destroy (hb_wasm_font_data_t *data HB_UNUSED)
{
}


/*
 * shaper
 */

hb_bool_t
_hb_wasm_shape (hb_shape_plan_t    *shape_plan,
		hb_font_t          *font,
		hb_buffer_t        *buffer,
		const hb_feature_t *features,
		unsigned int        num_features)
{
  if (unlikely (buffer->in_error ()))
    return false;

  bool ret = true;
  hb_face_t *face = font->face;
  const hb_wasm_face_data_t *face_data = face->data.wasm;

  bool retried = false;
  if (0)
  {
retry:
    DEBUG_MSG (WASM, font, "Retrying...");
  }

  wasm_function_inst_t func = nullptr;

  hb_wasm_shape_plan_t *plan = acquire_shape_plan (face, face_data);
  if (unlikely (!plan))
  {
    DEBUG_MSG (WASM, face_data, "Acquiring shape-plan failed.");
    return false;
  }

  auto *module_inst = plan->module_inst;
  auto *exec_env = plan->exec_env;

  // cmake -DWAMR_BUILD_REF_TYPES=1 for these to work
  HB_OBJ2REF (font);
  HB_OBJ2REF (buffer);
  if (unlikely (!fontref || !bufferref))
  {
    DEBUG_MSG (WASM, module_inst, "Failed to register objects.");
    goto fail;
  }

  func = wasm_runtime_lookup_function (module_inst, "shape", nullptr);
  if (unlikely (!func))
  {
    DEBUG_MSG (WASM, module_inst, "Shape function not found.");
    goto fail;
  }

  wasm_val_t results[1];
  wasm_val_t arguments[5];

  results[0].kind = WASM_I32;
  arguments[0].kind = WASM_I32;
  arguments[0].of.i32 = plan->wasm_shape_planptr;
  arguments[1].kind = WASM_I32;
  arguments[1].of.i32 = fontref;
  arguments[2].kind = WASM_I32;
  arguments[2].of.i32 = bufferref;
  arguments[3].kind = WASM_I32;
  arguments[3].of.i32 = wasm_runtime_module_dup_data (module_inst,
						      (const char *) features,
						      num_features * sizeof (features[0]));
  arguments[4].kind = WASM_I32;
  arguments[4].of.i32 = num_features;

  ret = wasm_runtime_call_wasm_a (exec_env, func,
				  ARRAY_LENGTH (results), results,
				  ARRAY_LENGTH (arguments), arguments);

  wasm_runtime_module_free (module_inst, arguments[2].of.i32);

  if (unlikely (!ret || !results[0].of.i32))
  {
    DEBUG_MSG (WASM, module_inst, "Calling shape() failed: %s",
	       wasm_runtime_get_exception (module_inst));
    if (retried)
      goto fail;
    buffer->successful = true;
    retried = true;
    release_shape_plan (face_data, plan);
    goto retry;
  }

  /* TODO Regularize clusters according to direction & cluster level,
   * such that client doesn't crash with unmet expectations. */

  if (!results[0].of.i32)
  {
fail:
    ret = false;
  }

  release_shape_plan (face_data, plan, ret);

  if (ret)
  {
    buffer->clear_glyph_flags ();
    buffer->unsafe_to_break ();
  }

  return ret;
}

#endif