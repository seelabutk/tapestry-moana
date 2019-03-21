// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

// ospray_sg
#include "sg/SceneGraph.h"
#include "sg/geometry/TriangleMesh.h"
#include "sg/visitor/MarkAllAsModified.h"
#include "sg/texture/Texture2D.h"
// ospapp
#include "ospapp/OSPApp.h"
// ospcommon
#include "ospcommon/utility/Optional.h"
#include "ospcommon/utility/StringManip.h"

#include "ChangeLightsVisibility.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "sg/3rdParty/stb_image_write.h"

#include <cstdio>

namespace ospray {
  namespace app {

    struct Foo {
      void write(void *data, int size) {
        unsigned char *d = (unsigned char *)data;
        bytes.insert(bytes.end(), d, d + size);
      }
    
      std::vector<unsigned char> bytes;
    };
    
    void write(void *context, void *data, int size) {
      Foo *myfoo = (Foo *)context;
      myfoo->write(data, size);
    }
    
    using LightPreset = std::pair<std::string, std::shared_ptr<sg::Node>>;
    using ospcommon::utility::Optional;

    struct OSPMoanaViewer : public OSPApp
    {
      OSPMoanaViewer();

    private:

      void render(const std::shared_ptr<sg::Frame> &) override;
      int parseCommandLine(int &ac, const char **&av) override;

      // Helper functions //

      void constructLightPresetsDropDownList();
      void updateLightsPreset(const std::shared_ptr<sg::Frame> &root);

      // Light preset importing helper functions //

      Optional<LightPreset>
      importLightsFromXML(const std::string &fileName);

      std::shared_ptr<sg::Node> createHDRILightFromXML(const xml::Node &node);
      std::shared_ptr<sg::Node> createAreaLightFromXML(const xml::Node &node);
      vec3f vec3fFromXMLContent(const std::string &str);

      // Data /////////////////////////////////////////////////////////////////

      std::vector<LightPreset> lightPresets;

      // NOTE(jda) - We have to hold on to light nodes that are getting
      //             overwritten when they are "reloaded" at runtime...otherwise
      //             we can cause nodes to be freed (calling ospRelease() on
      //             their handles) during the middle of an existing call to
      //             ospRenderFrame()
      std::vector<std::shared_ptr<sg::Node>> discardedLights;

      std::string dropDownTextList;

      int currentLightPreset = 0;

      bool fullscreen = false;
      float motionSpeed = -1.f;
      std::string initialTextForNodeSearch;

      vec3f default_vp;
      vec3f default_vi;
      vec3f default_vu;
      float default_focus {0.f};
      float default_fovy {0.f};
    };

    OSPMoanaViewer::OSPMoanaViewer()
    {
      initialRendererType = "pathtracer";

      ospLoadModule("ptex");
      ospLoadModule("biff");
    }

    void OSPMoanaViewer::render(const std::shared_ptr<sg::Frame> &root)
    {
      auto &camera = root->child("camera");

      default_vp    = camera["pos"].valueAs<vec3f>();
      default_vi    = camera["gaze"].valueAs<vec3f>();
      default_vu    = camera["up"].valueAs<vec3f>();
      default_focus = camera["focusDistance"].valueAs<float>();
      default_fovy  = camera["fovy"].valueAs<float>();

      auto &renderer = root->child("renderer");

      auto defaultLights = renderer["lights"].shared_from_this();
      lightPresets.emplace_back("default", defaultLights);

      currentLightPreset = lightPresets.size() - 1;
      updateLightsPreset(root);

      renderer["useBackplate"]      = false;
      renderer["useGeometryLights"] = false;

      FILE *f = fdopen(100, "w");
      char *line = NULL;
      size_t linesize = 0;
      ssize_t linelen;
      while ((linelen = getline(&line, &linesize, stdin)) > 0) {
        float x, y, z, ux, uy, uz, vx, vy, vz;
        int quality, tile_index, n_cols;
	int rv;
        if (12 != (rv = sscanf(line, "%f %f %f %f %f %f %f %f %f %d %d %d", &x, &y, &z, &ux, &uy, &uz, &vx, &vy, &vz, &quality, &tile_index, &n_cols))) {
          printf("oof %d\n", rv);
        }

	int w = quality;
	int h = quality;
	int tile_x = tile_index % n_cols;
	int tile_y = tile_index / n_cols;
  
        camera["pos"] = vec3f(x, y, z);
	camera["up"] = vec3f(ux, uy, uz);
	camera["dir"] = vec3f(vx, vy, vz);
	camera["imageEnd"] = vec2f(
		/* right */ ((float)tile_x + 1) / n_cols,
		/* top */ ((float)n_cols - tile_y) / n_cols
	);
	camera["imageStart"] = vec2f(
		/* left */ ((float)tile_x) / n_cols,
		/* bottom */ ((float)n_cols - tile_y - 1) / n_cols
	);
	camera.commit();

        std::shared_ptr<sg::FrameBuffer> fb = std::make_shared<sg::FrameBuffer>(vec2i(w, h));
        root->setChild("frameBuffer", fb);
        root->setChild("navFrameBuffer", fb);
        std::shared_ptr<sg::FrameBuffer> foo = root->renderFrame(true);
        float *pixels = (float *)foo->map();
  
        unsigned char *buffer = (unsigned char *)malloc(4 * w * h);
        for (int j=0; j<h; ++j) {
          float *rowIn = (float *)&pixels[4*(h-1-j)*w];
          for (int i=0; i<w; ++i) {
            int index = j * w + i;
            buffer[4*index+0] = (unsigned char)(rowIn[4*i+0] * 255.0f);
            buffer[4*index+1] = (unsigned char)(rowIn[4*i+1] * 255.0f);
            buffer[4*index+2] = (unsigned char)(rowIn[4*i+2] * 255.0f);
            buffer[4*index+3] = 255;
          }
        }
        
	foo->unmap(pixels);
  
        Foo myfoo;
        int ret = stbi_write_jpg_to_func(write, (void*)&myfoo, w, h, 4, buffer, 100);
        if (!ret) {
          printf("bad\n");
        }
        
	fprintf(f, "%lu:", myfoo.bytes.size());
        fwrite(myfoo.bytes.data(), 1, myfoo.bytes.size(), f);
	fprintf(f, ",");
        fflush(f);
      }

      fclose(f);

      lightPresets.clear();
      discardedLights.clear();
    }

    int OSPMoanaViewer::parseCommandLine(int &ac, const char **&av)
    {
      for (int i = 1; i < ac; i++) {
        const std::string arg = av[i];
        if (arg == "--fullscreen") {
          fullscreen = true;
          removeArgs(ac, av, i, 1);
          --i;
        } else if (arg == "--motionSpeed") {
          motionSpeed = atof(av[i + 1]);
          removeArgs(ac, av, i, 2);
          --i;
        } else if (arg == "--searchText") {
          initialTextForNodeSearch = av[i + 1];
          removeArgs(ac, av, i, 2);
          --i;
        } else if (arg == "--lights-file" || arg == "--lights-preset") {
          auto preset = importLightsFromXML(av[i + 1]);

          if (preset)
            lightPresets.push_back(preset.value());

          removeArgs(ac, av, i, 2);
          --i;
        }
      }
      return 0;
    }

    // Helper functions ///////////////////////////////////////////////////////

    void OSPMoanaViewer::constructLightPresetsDropDownList()
    {
      dropDownTextList.clear();
      for (auto &p : lightPresets) {
        dropDownTextList += FileName(p.first).base();
        dropDownTextList.push_back('\0');
      }
    }

    void
    OSPMoanaViewer::updateLightsPreset(const std::shared_ptr<sg::Frame> &root)
    {
      auto &renderer = root->child("renderer");

      auto preset = lightPresets[currentLightPreset].second;
      preset->traverse(sg::MarkAllAsModified{});

      renderer.setChild("lights", preset);
      renderer.markAsModified();
    }

    // Light preset importing helper functions ////////////////////////////////

    Optional<LightPreset>
    OSPMoanaViewer::importLightsFromXML(const std::string &fileName)
    {
      auto doc = xml::readXML(fileName);

      auto &xmlRoot = doc->child[0];
      auto &biff = xmlRoot.child[0];

      auto lightsGroupNode = sg::createNode("lights_" + fileName, "Node");
      int lightID = 0;

      for (const auto &node : biff.child) {
        std::shared_ptr<sg::Node> light;
        if (node.name == "light")
          light = createHDRILightFromXML(node);
        else if (node.name == "areaLight")
          light = createAreaLightFromXML(node);
        else {
          std::cerr << "WARNING: ignoring import of '" << node.name
                    << "' light!";
        }

        if (light.get() != nullptr) {
          light->setName(light->name() + std::to_string(lightID++));
          lightsGroupNode->add(light);
        }
      }

      if (lightID > 0) {
        lightsGroupNode->verify();
        return {LightPreset(fileName, lightsGroupNode)};
      } else {
        return {};
      }
    }

    std::shared_ptr<sg::Node>
    OSPMoanaViewer::createHDRILightFromXML(const xml::Node &node)
    {
      auto lightNode = sg::createNode("HDRILight_", "HDRILight");

      vec3f dir(3, 0, 6.43);
      float intensity(1.0f);

      std::string textureFileName;

      for (const auto &p : node.child) {
        auto name = p.properties.at("name");
        if (name == "mapname")
          textureFileName = p.content;
        else if (name == "name")
          lightNode->setName(p.content);
        else if (name == "dir")
          dir = vec3fFromXMLContent(p.content);
        else if (name == "intensity")
          intensity = std::atof(p.content.c_str());
      }

      auto tex = sg::Texture2D::load(textureFileName, false);

      if (tex.get() == nullptr) {
        std::cerr << "WARNING: could not load HDRI texture --> "
                  << textureFileName << std::endl;
        return {};
      }

      tex->setName("map");
      lightNode->add(tex);
      sg::Texture2D::clearTextureCache();

      lightNode->child("dir") = vec3f(-dir.z, dir.y, dir.x);
      lightNode->child("intensity") = intensity;

      return lightNode;
    }

    std::shared_ptr<sg::Node>
    OSPMoanaViewer::createAreaLightFromXML(const xml::Node &node)
    {
      auto lightNode = sg::createNode("areaLight_", "QuadLight");

      vec3f p0(0.f), p1(0.f), p2(0.f), p3(0.f), L(0.f), color(0.f);
      float intensity(0.f);
      auto useExposure = false;

      for (const auto &p : node.child) {
        auto name = p.properties.at("name");
        if (name == "p0")
          p0 = vec3fFromXMLContent(p.content);
        else if (name == "p1")
          p1 = vec3fFromXMLContent(p.content);
        else if (name == "p2")
          p2 = vec3fFromXMLContent(p.content);
        else if (name == "p3")
          p3 = vec3fFromXMLContent(p.content);
        else if (name == "L")
          L = vec3fFromXMLContent(p.content);
        else if (name == "name")
          lightNode->setName(p.content);
        else if (name == "exposure") {
          useExposure = true;
          intensity = std::pow(2.f, std::atof(p.content.c_str()));
        } else if (name == "color") {
          color = vec3fFromXMLContent(p.content);
          // gamma correction
          color.x = std::pow(color.x, 2.2f);
          color.y = std::pow(color.y, 2.2f);
          color.z = std::pow(color.z, 2.2f);
        }
      }

      lightNode->child("position")  = p0;
      lightNode->child("edge1")     = p1 - p0;
      lightNode->child("edge2")     = p3 - p0;

      // Only use L value if exposure/color aren't set.
      if (!useExposure) {
        intensity = reduce_max(L);
        color = L / intensity;
      }

      lightNode->child("color")     = intensity > 0 ? color : vec3f(1.f);
      lightNode->child("intensity") = intensity;

      lightNode->createChild("isVisible", "bool") = false;

      return lightNode;
    }

    vec3f OSPMoanaViewer::vec3fFromXMLContent(const std::string &str)
    {
      vec3f retval;

      auto components = utility::split(str, ' ');

      retval.x = std::atof(components[0].c_str());
      retval.y = std::atof(components[1].c_str());
      retval.z = std::atof(components[2].c_str());

      return retval;
    }

  } // ::ospray::app
} // ::ospray

int main(int ac, const char **av)
{
  ospray::app::OSPMoanaViewer ospApp;
  return ospApp.main(ac, av);
}
