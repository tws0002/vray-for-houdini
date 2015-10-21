//
// Copyright (c) 2015, Chaos Software Ltd
//
// V-Ray For Houdini
//
// ACCESSIBLE SOURCE CODE WITHOUT DISTRIBUTION OF MODIFICATION LICENSE
//
// Full license text: https://github.com/ChaosGroup/vray-for-houdini/blob/master/LICENSE
//

#include "vfh_render.h"
#include "vfh_exporter.h"
#include "vfh_prm_globals.h"
#include "vfh_prm_json.h"
#include "vfh_prm_def.h"
#include "vfh_prm_templates.h"
#include "vfh_ui.h"

#include <ROP/ROP_Templates.h>
#include <UT/UT_Interrupt.h>

#include <boost/bind.hpp>


using namespace VRayForHoudini;


static PRM_Name     parm_render_vfb_mode("render_vfb_mode", "Framebuffer");
static PRM_Name     parm_render_vfb_mode_items[] = {
	PRM_Name("Native"),
	PRM_Name("Simple"),
	PRM_Name(),
};
static PRM_ChoiceList parm_render_vfb_mode_menu(PRM_CHOICELIST_SINGLE, parm_render_vfb_mode_items);

static PRM_Name     parm_render_sep_render("render_sep_render", "Render Settings");
static PRM_Name     parm_render_camera("render_camera", "Camera");
static PRM_Default  parm_render_camera_def(0, "/obj/cam1");

static PRM_Name     parm_render_sep_export("render_sep_export", "Export Settings");
static PRM_Name     parm_render_export_path("render_export_filepath", "Export Filepath");
static PRM_Default  parm_render_export_path_def(0, "$HIP/$HIPNAME.vrscene");

static PRM_Name     parm_render_export_mode("render_export_mode", "Export Mode");
static PRM_Name     parm_render_export_mode_items[] = {
	PRM_Name("Render"),
	PRM_Name("Export & Render"),
	PRM_Name("Export"),
	PRM_Name(),
};
static PRM_ChoiceList parm_render_export_mode_menu(PRM_CHOICELIST_SINGLE, parm_render_export_mode_items);

static PRM_Name     parm_render_render_mode("render_render_mode", "Render Mode");
static PRM_Name     parm_render_render_mode_items[] = {
	PRM_Name("Production"),
	PRM_Name("RT (CPU)"),
	PRM_Name("RT (CUDA)"),
	PRM_Name(),
};
static PRM_ChoiceList parm_render_render_mode_menu(PRM_CHOICELIST_SINGLE, parm_render_render_mode_items);

static PRM_Name  parm_render_sep_networks("render_sep_networks", "Networks");
static PRM_Name  parm_render_net_render_channels("render_network_render_channels", "Render Channels");
static PRM_Name  parm_render_net_environment("render_network_environment", "Environment");

static PRM_Name          RenderSettingsSwitcherName("VRayRenderSettings");
static Parm::PRMDefList  RenderSettingsSwitcherTabs;
static Parm::PRMTmplList RenderSettingsPrmTemplate;

static Parm::TabItemDesc RenderSettingsTabItemsDesc[] = {
	{ "Options",        "SettingsOptions"          },
	{ "Output",         "SettingsOutput"           },
	{ "Color Mapping",  "SettingsColorMapping"     },
	{ "DMC Sampler",    "SettingsDMCSampler"       },
	{ "Image Sampler",  "SettingsImageSampler"     },
	{ "GI",             "SettingsGI"               },
	{ "Irradiance Map", "SettingsIrradianceMap"    },
	{ "Light Cache",    "SettingsLightCache"       },
	{ "Brute Force",    "SettingsDMCGI"            },
	{ "Raycaster",      "SettingsRaycaster"        },
	{ "Regions",        "SettingsRegionsGenerator" },
	{ "Camera",         "SettingsCamera"           },
	{ "DOF",            "SettingsCameraDof"        },
	{ "Motion Blur",    "SettingsMotionBlur"       },
	{ "RT",             "SettingsRTEngine"         }
};

static PRM_Template* getTemplates()
{
	if (!RenderSettingsPrmTemplate.size()) {
		// Render / Exporter settings
		//
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_HEADING, 1, &parm_render_sep_render));
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_STRING_E, PRM_TYPE_DYNAMIC_PATH, 1, &parm_render_camera, &parm_render_camera_def));
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_ORD, 1, &parm_render_render_mode, PRMzeroDefaults, &parm_render_render_mode_menu));
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_ORD, 1, &parm_render_export_mode, PRMzeroDefaults, &parm_render_export_mode_menu));
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_ORD, 1, &parm_render_vfb_mode, PRMzeroDefaults, &parm_render_vfb_mode_menu));

		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_HEADING, 1, &parm_render_sep_export));
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_FILE_E, PRM_TYPE_DYNAMIC_PATH, 1, &parm_render_export_path, &parm_render_export_path_def));

		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_HEADING, 1, &parm_render_sep_networks));
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_STRING_E, PRM_TYPE_DYNAMIC_PATH, 1, &parm_render_net_render_channels, &Parm::PRMemptyStringDefault));
		RenderSettingsPrmTemplate.push_back(PRM_Template(PRM_STRING_E, PRM_TYPE_DYNAMIC_PATH, 1, &parm_render_net_environment,     &Parm::PRMemptyStringDefault));

		RenderSettingsSwitcherTabs.push_back(PRM_Default(RenderSettingsPrmTemplate.size(), "Globals"));

		// Renderer settings
		//
		Parm::addTabItems(RenderSettingsTabItemsDesc, CountOf(RenderSettingsTabItemsDesc), RenderSettingsSwitcherTabs, RenderSettingsPrmTemplate);

		// Standard ROP settings
		//
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_TPRERENDER_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_PRERENDER_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_LPRERENDER_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_TPREFRAME_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_PREFRAME_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_LPREFRAME_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_TPOSTFRAME_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_POSTFRAME_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_LPOSTFRAME_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_TPOSTRENDER_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_POSTRENDER_TPLATE]);
		RenderSettingsPrmTemplate.push_back(theRopTemplates[ROP_LPOSTRENDER_TPLATE]);
		RenderSettingsSwitcherTabs.push_back(PRM_Default(12, "Scripts"));

		RenderSettingsPrmTemplate.push_back(PRM_Template()); // List terminator

		// Main switcher menu
		RenderSettingsPrmTemplate.insert(RenderSettingsPrmTemplate.begin(),
										 PRM_Template(PRM_SWITCHER,
													  RenderSettingsSwitcherTabs.size(),
													  &RenderSettingsSwitcherName,
													  &RenderSettingsSwitcherTabs[0]));
	}

	return &RenderSettingsPrmTemplate[0];
}


OP_TemplatePair* VRayRendererNode::getTemplatePair()
{
	static OP_TemplatePair *ropPair = 0;
	if (!ropPair) {
		OP_TemplatePair *base = new OP_TemplatePair(getTemplates());
		ropPair = new OP_TemplatePair(ROP_Node::getROPbaseTemplate(), base);
	}
	return ropPair;
}


VRayRendererNode::VRayRendererNode(OP_Network *net, const char *name, OP_Operator *entry)
	: ROP_Node(net, name, entry)
	, m_exporter(this)
	, m_error(ROP_CONTINUE_RENDER)
{
}


VRayRendererNode::~VRayRendererNode()
{
	PRINT_WARN("~VRayRendererNode()");

#if 0
	m_exporter.delOpCallback(this, VRayRendererNode::RtCallbackRop);
#endif
}


bool VRayRendererNode::updateParmsFlags()
{
	bool changed = ROP_Node::updateParmsFlags();

	bool gi_on = evalInt("SettingsGI.on", 0, 0);
#if 0
	for (const auto tab : RenderSettingsTabs) {
		PRM_Template *prm = tab.items;

		while (prm->getType() != PRM_LIST_TERMINATOR) {
			if (Parm::RenderGIPlugins.count(tab.pluginID)) {
				bool process_param = true;

				// On "SettingsGI" tab activate / deactivate
				// everything except "on" parameter
				//
				if (tab.pluginID == "SettingsGI") {
					if (StrEq(prm->getToken(), "SettingsGI.on")) {
						process_param = false;
					}
				}
				if (process_param) {
					changed |= enableParm(prm->getToken(), gi_on);
				}
			}
			prm++;
		}

		UI::ActiveStateDeps::activateElements(tab.pluginID, this, changed);
	}
#endif
	return changed;
}


void VRayRendererNode::RtCallbackRop(OP_Node *caller, void *callee, OP_EventType type, void *data)
{
	VRayRendererNode *rop = (VRayRendererNode*)callee;

	PRINT_INFO("RtCallbackRop: %s from \"%s\"",
			   OPeventToString(type), caller->getName().buffer());

	if (type == OP_NODE_PREDELETE) {
		caller->removeOpInterest(rop, VRayRendererNode::RtCallbackRop);
	}
}


int VRayRendererNode::startRender(int nframes, fpreal tstart, fpreal tend)
{
	PRINT_WARN("VRayRendererNode::startRender(%i, %.3ff, %.3f)",
			   nframes, tstart, tend);

	m_frames     = nframes;
	m_time_start = tstart;
	m_time_end   = tend;

	m_exportedFrames.clear();

	// [ ] Add option to keep data
	// [ ] Clear keyframes based on this options
	//
	// clearKeyFrames();

	OP_Node *camera = VRayExporter::GetCamera(this);
	if (!camera) {
		PRINT_ERROR("Camera is not set!");

		m_error = ROP_ABORT_RENDER;
	}
	else {
		int renderMode = evalInt(parm_render_render_mode.getToken(), 0, 0.0);
		if (renderMode == 0) {
			renderMode = -1;
		}
		else if (renderMode == 1) {
			renderMode = 0;
		}
		else if (renderMode == 2) {
			renderMode = 4;
		}

		m_exporter.getRenderer().resetCallbacks();
		if (m_exporter.init(renderMode)) {
			PRINT_ERROR("V-Ray is not initialized!");

			m_error = ROP_ABORT_RENDER;
		}
		else {
			const bool is_animation = m_frames > 1;

			VRayExporter::ExpWorkMode workMode = (VRayExporter::ExpWorkMode)evalInt(parm_render_export_mode.getToken(), 0, 0.0);

			UT_String exportFilepath;
			evalString(exportFilepath, parm_render_export_path.getToken(), 0, 0.0);

			executePreRenderScript(m_time_start);

#if 0
			if (!hasOpInterest(this, VRayRendererNode::RtCallbackRop)) {
				addOpInterest(this, VRayRendererNode::RtCallbackRop);
			}
#endif

			m_exporter.setMode(renderMode);
			m_exporter.setAnimation(is_animation);
			m_exporter.setWorkMode(workMode);
			m_exporter.setExportFilepath(exportFilepath);

#ifdef __APPLE__
			const int useVFB = 1;
#else
			const int useVFB = evalInt(parm_render_vfb_mode.getToken(), 0, 0.0);
#endif
			if (useVFB == 0) {
#ifndef __APPLE__
				m_vfb.free();
				m_exporter.getRenderer().showVFB(true);
#endif
			}
			else if (useVFB == 1) {
#ifndef __APPLE__
				m_exporter.getRenderer().showVFB(false);
#endif
				m_vfb.init();
				// m_vfb.resize();
				m_vfb.show();

				m_vfb.set_abort_callback(UI::AbortCb(boost::bind(&VRayPluginRenderer::stopRender, &m_exporter.getRenderer())));

				m_exporter.getRenderer().addCbOnDumpMessage(CbOnDumpMessage(boost::bind(&UI::VFB::on_dump_message, &m_vfb, _1, _2, _3)));
				m_exporter.getRenderer().addCbOnProgress(CbOnProgress(boost::bind(&UI::VFB::on_progress, &m_vfb, _1, _2, _3, _4)));

				m_exporter.getRenderer().addCbOnImageReady(CbOnImageReady(boost::bind(&UI::VFB::on_image_ready, &m_vfb, _1)));

				m_exporter.getRenderer().addCbOnBucketInit(CbOnBucketInit(boost::bind(&UI::VFB::on_bucket_init, &m_vfb, _1, _2, _3, _4, _5, _6)));
				m_exporter.getRenderer().addCbOnBucketFailed(CbOnBucketFailed(boost::bind(&UI::VFB::on_bucket_failed, &m_vfb, _1, _2, _3, _4, _5, _6)));
				m_exporter.getRenderer().addCbOnBucketReady(CbOnBucketReady(boost::bind(&UI::VFB::on_bucket_ready, &m_vfb, _1, _2, _3, _4, _5)));

				m_exporter.getRenderer().addCbOnRTImageUpdated(CbOnRTImageUpdated(boost::bind(&UI::VFB::on_rt_image_updated, &m_vfb, _1, _2)));

			}

			if (is_animation) {
				m_exporter.addAbortCallback();
			}
			else {
				if (m_exporter.isRt()) {
					m_exporter.addRtCallbacks();
				}
				else {
					m_exporter.removeRtCallbacks();
				}
			}

			m_exporter.exportSettings(this);

			m_error = ROP_CONTINUE_RENDER;
		}
	}

	PRINT_WARN("VRayRendererNode::startRender finished with %i",
			   m_error);

	return m_error;
}


ROP_RENDER_CODE VRayRendererNode::renderFrame(fpreal time, UT_Interrupt *boss)
{
	OP_Context context;
	context.setTime(time);

	PRINT_WARN("VRayRendererNode::renderFrame(%.3f)",
			   context.getFloatFrame());

	if (m_error == ROP_ABORT_RENDER) {
		PRINT_ERROR("Rendering initialization error!");
	}
	else {
		const bool is_animation = m_frames > 1;
		const bool use_motion_blur = evalInt("SettingsMotionBlur.on", 0, 0.0);

		if (is_animation && use_motion_blur) {
			int mb_geom_samples = 1;

			// Duration in frames
			fpreal mb_duration = 0.0f;

			// Duration interval center
			fpreal mb_interval_center = 0.0f;

			const bool is_camera_physical = false;
			if (is_camera_physical) {
				// TODO: PhysicalCamera mb
			}
			else {
				mb_duration        = evalFloat("SettingsMotionBlur.duration", 0, 0.0);
				mb_interval_center = evalFloat("SettingsMotionBlur.interval_center", 0, 0.0);
				mb_geom_samples    = evalInt("SettingsMotionBlur.geom_samples", 0, 0.0);
			}

			// Export motion blur interval
			const fpreal frame_current = context.getFloatFrame();

			PRINT_WARN("Frame current: %.3f",
					   frame_current);

			fpreal mb_start = frame_current - (mb_duration * (0.5 - mb_interval_center));
			fpreal mb_end   = mb_start + mb_duration;

			fpreal mb_frame_inc = mb_duration / (mb_geom_samples + 1);

			PRINT_WARN("  MB duration: %.3f", mb_duration);
			PRINT_WARN("  MB interval center: %.3f", mb_interval_center);
			PRINT_WARN("  MB geom samples: %i", mb_geom_samples);

			PRINT_WARN("  MB start: %.3f", mb_start);
			PRINT_WARN("  MB end:   %.3f", mb_end);
			PRINT_WARN("  MB inc:   %.3f", mb_frame_inc);

			// We don't need this data anymore
			clearKeyFrames(mb_start);

			for (FloatSet::iterator tIt = m_exportedFrames.begin(); tIt != m_exportedFrames.end();) {
				if (*tIt < mb_start) {
					m_exportedFrames.erase(tIt++);
				}
				else {
					++tIt;
				}
			}

			// Export motion blur data
			fpreal subframe = mb_start;
			while (subframe <= mb_end) {
				if (m_exporter.isAborted()) {
					break;
				}
				if (!m_exportedFrames.count(subframe)) {
					m_exportedFrames.insert(subframe);

					context.setFrame(subframe);

					PRINT_WARN("Exporting motion blur sub-frame: %.3f [time=%.3f]",
							   context.getFloatFrame(), context.getTime());

					exportKeyFrame(context);
				}
				subframe += mb_frame_inc;
			}

			// Set time back to original time for rendering
			context.setTime(time);
		}
		else {
			PRINT_WARN("Exporting frame: %.3f",
					   context.getFloatFrame());

			clearKeyFrames(context.getFloatFrame());
			exportKeyFrame(context);
		}

		if (m_exporter.isAborted()) {
			PRINT_WARN("Operation is aborted by the user!")
					m_error = ROP_ABORT_RENDER;
		}
		else {
			m_exporter.setFrame(context.getFloatFrame());
			renderKeyFrame(context.getFloatFrame(), is_animation);
		}
	}

	PRINT_WARN("VRayRendererNode::renderFrame finished with %i",
			   m_error);

	return m_error;
}


ROP_RENDER_CODE VRayRendererNode::endRender()
{
	PRINT_WARN("VRayRendererNode::endRender()");

	m_exporter.exportDone();

	clearKeyFrames(SYS_FP64_MAX);

	executePostRenderScript(m_time_end);

	PRINT_WARN("VRayRendererNode::endRender finished with %i",
			   m_error);

	return ROP_CONTINUE_RENDER;
}


int VRayRendererNode::renderKeyFrame(fpreal time, int locked)
{
	PRINT_INFO("VRayRendererNode::renderKeyFrame(%.3f)",
			   time);

	m_exporter.renderFrame(locked);

	return 0;
}


int VRayRendererNode::exportKeyFrame(const OP_Context &context)
{
	// Execute the pre-render script.
	executePreFrameScript(context.getTime());

	m_exporter.setFrame(context.getFloatFrame());
	m_exporter.setContext(context);

	int err = m_exporter.exportView();
	if (err) {
		m_error = ROP_ABORT_RENDER;
	}
	else {
		m_exporter.exportScene();

		UT_String env_network_path;
		evalString(env_network_path, parm_render_net_environment.getToken(), 0, 0.0f);
		if (NOT(env_network_path.equal(""))) {
			OP_Node *env_network = OPgetDirector()->findNode(env_network_path.buffer());
			if (env_network) {
				OP_Node *env_node = VRayExporter::FindChildNodeByType(env_network, "VRayNodeSettingsEnvironment");
				if (NOT(env_node)) {
					PRINT_ERROR("Node of type \"VRay SettingsEnvironment\" is not found!");
				}
				else {
					m_exporter.exportEnvironment(env_node);
					m_exporter.exportEffects(env_network);
				}
			}
		}

		UT_String channels_network_path;
		evalString(channels_network_path, parm_render_net_render_channels.getToken(), 0, 0.0f);
		if (NOT(channels_network_path.equal(""))) {
			OP_Node *channels_network = OPgetDirector()->findNode(channels_network_path.buffer());
			if (channels_network) {
				OP_Node *chan_node = VRayExporter::FindChildNodeByType(channels_network, "VRayNodeRenderChannelsContainer");
				if (NOT(chan_node)) {
					PRINT_ERROR("Node of type \"VRay RenderChannelsContainer\" is not found!");
				}
				else {
					m_exporter.exportRenderChannels(chan_node);
				}
			}
		}
	}

	executePostFrameScript(context.getTime());

	return err;
}


int VRayRendererNode::clearKeyFrames(fpreal toTime)
{
	PRINT_ERROR("VRayRendererNode::clearKeyFrames(%.3f)",
				toTime);

	return m_exporter.clearKeyFrames(toTime);
}


void VRayRendererNode::register_operator(OP_OperatorTable *table)
{
	OP_Operator *rop = new OP_Operator(/* Internal name     */ "vray_renderer",
									   /* UI name           */ "V-Ray Renderer",
									   /* How to create one */ VRayRendererNode::myConstructor,
									   /* Parm definitions  */ VRayRendererNode::getTemplatePair(),
									   /* Min # of inputs   */ 0,
									   /* Max # of inputs   */ 0);

	// Set icon
	rop->setIconName("ROP_vray");

	table->addOperator(rop);
}
