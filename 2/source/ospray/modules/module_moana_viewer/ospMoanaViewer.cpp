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

#ifdef OSPRAY_APPS_ENABLE_DENOISER
#include <OpenImageDenoise/oidn.hpp>
#endif

#include <cstdio>
#include <algorithm>

namespace ospray {
  namespace app {

    inline float linear_to_srgb(const float f) {
      const float c = std::max(f, 0.f);
      return c <= 0.0031308f ? 12.92f*c : std::pow(c, 1.f/2.4f)*1.055f - 0.055f;
    }

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
#ifdef OSPRAY_APPS_ENABLE_DENOISER
      oidn::DeviceRef dev = oidn::newDevice();
      dev.commit();
      oidn::FilterRef filter = dev.newFilter("RT");
      std::vector<vec4f> output;
#endif

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
        int width, height, tile_index, n_cols, n_rows;
        float fovy;
	int rv;
        if (14 != (rv = sscanf(line, "%f %f %f %f %f %f %f %f %f %d %d %d %d %d %f", &x, &y, &z, &ux, &uy, &uz, &vx, &vy, &vz, &width, &height, &tile_index, &n_cols, &n_rows, &fovy))) {
          fprintf(stderr, "oof %d\n", rv);
        }

        int w = width;
        int h = height;
        int tile_x = tile_index % n_cols;
        int tile_y = tile_index / n_cols;
  
        camera["pos"] = vec3f(x, y, z);
        camera["up"] = vec3f(ux, uy, uz);
        camera["dir"] = vec3f(vx, vy, vz);
        camera["imageEnd"] = vec2f(
          /* right */ ((float)tile_x + 1) / n_cols,
          /* top */ ((float)n_rows - tile_y) / n_rows
        );
        camera["imageStart"] = vec2f(
          /* left */ ((float)tile_x) / n_cols,
          /* bottom */ ((float)n_rows - tile_y - 1) / n_rows
        );
        camera["fovy"] = fovy;
        camera.commit();

        std::shared_ptr<sg::FrameBuffer> fb = std::make_shared<sg::FrameBuffer>(vec2i(w, h));
#ifdef OSPRAY_APPS_ENABLE_DENOISER
        fb->operator[]("useDenoiser") = true;
#endif
        root->setChild("frameBuffer", fb);
        root->setChild("navFrameBuffer", fb);
        std::shared_ptr<sg::FrameBuffer> foo = root->renderFrame(true);

        bool hdr = !fb->toneMapped();
#ifdef OSPRAY_APPS_ENABLE_DENOISER
        filter.set("hdr", hdr);
#endif

#ifdef OSPRAY_APPS_ENABLE_DENOISER
        {
          const char *name = "color";
          void *ptr = (void *)foo->map(OSP_FB_COLOR);
          oidn::Format format = oidn::Format::Float3;
          size_t width = w;
          size_t height = h;
          size_t byteOffset = 0;
          size_t bytePixelStride = sizeof(vec4f);
          size_t byteRowStride = w * bytePixelStride;
          filter.setImage(name, ptr, format, width, height, byteOffset, bytePixelStride, byteRowStride);
          foo->unmap(ptr);
        }

        {
          const char *name = "normal";
          void *ptr = (void *)foo->map(OSP_FB_NORMAL);
          oidn::Format format = oidn::Format::Float3;
          size_t width = w;
          size_t height = h;
          size_t byteOffset = 0;
          size_t bytePixelStride = sizeof(vec3f);
          size_t byteRowStride = w * bytePixelStride;
          filter.setImage(name, ptr, format, width, height, byteOffset, bytePixelStride, byteRowStride);
          foo->unmap(ptr);
        }

        {
          const char *name = "albedo";
          void *ptr = (void *)foo->map(OSP_FB_ALBEDO);
          oidn::Format format = oidn::Format::Float3;
          size_t width = w;
          size_t height = h;
          size_t byteOffset = 0;
          size_t bytePixelStride = sizeof(vec3f);
          size_t byteRowStride = w * bytePixelStride;
          filter.setImage(name, ptr, format, width, height, byteOffset, bytePixelStride, byteRowStride);
          foo->unmap(ptr);
        }

        {
          const char *name = "output";
          output.resize(w * h);
          void *ptr = output.data();
          oidn::Format format = oidn::Format::Float3;
          size_t width = w;
          size_t height = h;
          size_t byteOffset = 0;
          size_t bytePixelStride = sizeof(vec4f);
          size_t byteRowStride = w * bytePixelStride;
          filter.setImage(name, ptr, format, width, height, byteOffset, bytePixelStride, byteRowStride);
        }

        filter.commit();
        filter.execute();
        float *pixels = (float *)output.data();

        // Save raw data to disk
        if (0) {
          static int _temp = 0;
          if (_temp == 0) { srand(time(NULL)); _temp = rand(); }
          char tempname[256];
          snprintf(tempname, 256, "/out/image.%d.bin", _temp++);
          FILE *f = fopen(tempname, "w");
          for (int i=0; i<h; ++i)
          for (int j=0; j<w; ++j) {
            fwrite(&pixels[4*j+4*w*i+0], sizeof(float), 1, f);
            fwrite(&pixels[4*j+4*w*i+1], sizeof(float), 1, f);
            fwrite(&pixels[4*j+4*w*i+2], sizeof(float), 1, f);
            fwrite(&pixels[4*j+4*w*i+3], sizeof(float), 1, f);
          }
          fclose(f);
        }
  
        unsigned char *buffer = (unsigned char *)malloc(4 * w * h);

        // XXX: use a constant normalization factor since each process only sees
        // a single tile, not the whole image.  Normalization has to be
        // consistent across all tiles.
        float normalize = 1.f/2.3f; // 2.3 seems a decent value with filmic tonemapping
        for (int j=0; j<h; ++j) {
          float *rowIn = (float *)&pixels[4*(h-1-j)*w];
          for (int i=0; i<w; ++i) {
            int index = j * w + i;
            buffer[4*index+0] = (unsigned char)(255.0f * linear_to_srgb(rowIn[4*i+0] * normalize));
            buffer[4*index+1] = (unsigned char)(255.0f * linear_to_srgb(rowIn[4*i+1] * normalize));
            buffer[4*index+2] = (unsigned char)(255.0f * linear_to_srgb(rowIn[4*i+2] * normalize));
            buffer[4*index+3] = 255;
          }
        }

#else
        float *pixels = (float *)foo->map();

        unsigned char *buffer = (unsigned char *)malloc(4 * w * h);
        for (int j=0; j<h; ++j) {
          float *rowIn = (float *)&pixels[4*(h-1-j)*w];
          for (int i=0; i<w; ++i) {
            int index = j * w + i;
            buffer[4*index+0] = (unsigned char)(255.0f * (rowIn[4*i+0]));
            buffer[4*index+1] = (unsigned char)(255.0f * (rowIn[4*i+1]));
            buffer[4*index+2] = (unsigned char)(255.0f * (rowIn[4*i+2]));
            buffer[4*index+3] = 255;
          }
        }
        foo->unmap(pixels);
#endif
  
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
