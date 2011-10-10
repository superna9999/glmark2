/*
 * Copyright © 2010-2011 Linaro Limited
 *
 * This file is part of the glmark2 OpenGL (ES) 2.0 benchmark.
 *
 * glmark2 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * glmark2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * glmark2.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *  Alexandros Frantzis (glmark2)
 */
#include "scene.h"
#include "log.h"
#include "mat.h"
#include "stack.h"
#include "shader-source.h"
#include "util.h"
#include "gl-headers.h"
#include <cmath>

/***********************
 * Wave implementation *
 ***********************/

/** 
 * A callback used to set up the grid by the Wave class.
 * It is called for each "quad" of the grid.
 */
static void
wave_grid_conf(Mesh &mesh, int x, int y, int n_x, int n_y,
               LibMatrix::vec3 &ul,
               LibMatrix::vec3 &ll,
               LibMatrix::vec3 &ur,
               LibMatrix::vec3 &lr)
{
    (void)x; (void)y; (void)n_x; (void)n_y;

    /* 
     * Order matters here, so that Wave::vertex_length_index() can work.
     * Vertices of the triangles at index i that belong to length index i
     * are even, those that belong to i + 1 are odd.
     */
    const LibMatrix::vec3* t[] = {
        &ll, &ur, &ul, &ur, &ll, &lr
    };

    for (int i = 0; i < 6; i++) {
        mesh.next_vertex();
        /* 
         * Set the vertex position and the three vertex positions
         * of the triangle this vertex belongs to.
         */
        mesh.set_attrib(0, *t[i]);
        mesh.set_attrib(1, *t[3 * (i / 3)]);
        mesh.set_attrib(2, *t[3 * (i / 3) + 1]);
        mesh.set_attrib(3, *t[3 * (i / 3) + 2]);
    }
}

/** 
 * Renders a grid mesh modulated by a sine wave
 */
class WaveMesh
{
public:
    /** 
     * Creates a wave mesh.
     * 
     * @param length the total length of the grid (in model coordinates)
     * @param width the total width of the grid (in model coordinates)
     * @param nlength the number of length-wise grid subdivisions
     * @param nwidth the number of width-wise grid subdivisions
     * @param wavelength the wave length as a proportion of the length
     * @param duty_cycle the duty cycle ()
     */
    WaveMesh(double length, double width, size_t nlength, size_t nwidth,
             double wavelength, double duty_cycle) :
        length_(length), width_(width), nlength_(nlength), nwidth_(nwidth),
        wave_k_(2 * M_PI / (wavelength * length)),
        wave_period_(2.0 * M_PI / wave_k_),
        wave_full_period_(wave_period_ / duty_cycle),
        wave_velocity_(0.1 * length), displacement_(nlength + 1)
    {
        create_program();
        create_mesh();
    }


    ~WaveMesh() { reset(); }

    /** 
     * Updates the state of a wave mesh.
     * 
     * @param elapsed the time elapsed since the beginning of the rendering
     */
    void update(double elapsed)
    {
        std::vector<std::vector<float> >& vertices(mesh_.vertices());

        /* Figure out which length index ranges need update */
        std::vector<std::pair<size_t, size_t> > ranges;

        for (size_t n = 0; n <= nlength_; n++) {
            double d(displacement(n, elapsed));

            if (d != displacement_[n]) {
                if (ranges.size() > 0 && ranges.back().second == n - 1) {
                    ranges.back().second = n;
                }
                else {
                    ranges.push_back(
                            std::pair<size_t, size_t>(n > 0 ? n - 1 : 0, n)
                            );
                }
            }

            displacement_[n] = d;
        }

        /* Update the vertex data of the changed ranges */
        for (std::vector<std::pair<size_t, size_t> >::iterator iter = ranges.begin();
             iter != ranges.end();
             iter++)
        {
            /* First vertex of length index range */
            size_t vstart(iter->first * nwidth_ * 6 + (iter->first % 2));
            /* 
             * First vertex not included in the range. We should also update all
             * vertices of triangles touching index i.
             */
            size_t vend((iter->second + (iter->second < nlength_)) * nwidth_ * 6);

            for (size_t v = vstart; v < vend; v++) {
                size_t vt = 3 * (v / 3);
                vertices[v][0 * 3 + 2] = displacement_[vertex_length_index(v)];
                vertices[v][1 * 3 + 2] = displacement_[vertex_length_index(vt)];
                vertices[v][2 * 3 + 2] = displacement_[vertex_length_index(vt + 1)];
                vertices[v][3 * 3 + 2] = displacement_[vertex_length_index(vt + 2)];
            }

            /* Update pair with actual vertex range */
            iter->first = vstart;
            iter->second = vend - 1;
        }

        mesh_.update_vbo(ranges);
    }

    Mesh& mesh() { return mesh_; }
    Program& program() { return program_; }

    void reset()
    {
        program_.stop();
        program_.release();
        mesh_.reset();
    }

private:
    Mesh mesh_;
    Program program_;
    double length_;
    double width_;
    size_t nlength_;
    size_t nwidth_;
    /* Wave parameters */
    double wave_k_;
    double wave_period_;
    double wave_full_period_;
    double wave_fill_;
    double wave_velocity_;

    std::vector<double> displacement_;

    /** 
     * Calculates the length index of a vertex.
     */
    size_t vertex_length_index(size_t v)
    {
        return v / (6 * nwidth_) + (v % 2);
    }

    /** 
     * The sine wave function with duty-cycle.
     *
     * @param x the space coordinate
     * 
     * @return the operation error code
     */
    double wave_func(double x)
    {
        double r(fmod(x, wave_full_period_));
        if (r < 0)
            r += wave_full_period_;

        /* 
         * Return either the sine value or 0.0, depending on the
         * wave duty cycle.
         */ 
        if (r > wave_period_)
        {
            return 0;
        }
        else
        {
            return 0.2 * std::sin(wave_k_ * r);
        }
    }

    /** 
     * Calculates the displacement of the wave.
     * 
     * @param n the length index
     * @param elapsed the time elapsed since the beginning of the rendering
     * 
     * @return the displacement at point n at time elapsed
     */
    double displacement(size_t n, double elapsed)
    {
        double x(n * length_ / nlength_);

        return wave_func(x - wave_velocity_ * elapsed);
    }

    /** 
     * Creates the GL shader program.
     */
    void create_program()
    {
        /* Set up shaders */
        static const std::string vtx_shader_filename(
                GLMARK_DATA_PATH"/shaders/buffer-wireframe.vert");
        static const std::string frg_shader_filename(
                GLMARK_DATA_PATH"/shaders/buffer-wireframe.frag");

        ShaderSource vtx_source(vtx_shader_filename);
        ShaderSource frg_source(frg_shader_filename);

        if (!Scene::load_shaders_from_strings(program_, vtx_source.str(),
                                              frg_source.str()))
        {
            return;
        }
    }

    /** 
     * Creates the grid mesh.
     */
    void create_mesh()
    {
        /* 
         * We need to pass the positions of all vertex of the triangle
         * in order to draw the wireframe.
         */
        std::vector<int> vertex_format;
        vertex_format.push_back(3);     // Position of vertex
        vertex_format.push_back(3);     // Position of triangle vertex 0
        vertex_format.push_back(3);     // Position of triangle vertex 1
        vertex_format.push_back(3);     // Position of triangle vertex 2
        mesh_.set_vertex_format(vertex_format);

        std::vector<GLint> attrib_locations;
        attrib_locations.push_back(program_["position"].location());
        attrib_locations.push_back(program_["tvertex0"].location());
        attrib_locations.push_back(program_["tvertex1"].location());
        attrib_locations.push_back(program_["tvertex2"].location());
        mesh_.set_attrib_locations(attrib_locations);

        mesh_.make_grid(nlength_, nwidth_, length_, width_,
                        0.0, wave_grid_conf);
    }

};

/******************************
 * SceneBuffer implementation *
 ******************************/

struct SceneBufferPrivate {
    WaveMesh *wave;
    ~SceneBufferPrivate() { delete wave; }
};

SceneBuffer::SceneBuffer(Canvas &pCanvas) :
    Scene(pCanvas, "buffer")
{
    priv_ = new SceneBufferPrivate();
    mOptions["interleave"] = Scene::Option("interleave", "false",
                                           "Whether to interleave vertex attribute data [true,false]");
    mOptions["update-method"] = Scene::Option("update-method", "map",
                                              "[map,subdata]");
    mOptions["update-fraction"] = Scene::Option("update-fraction", "1.0",
                                                "The fraction of the mesh length that is updated at every iteration (0.0-1.0)");
    mOptions["update-dispersion"] = Scene::Option("update-dispersion", "0.0",
                                                  "How dispersed the updates are [0.0 - 1.0]");
    mOptions["columns"] = Scene::Option("columns", "100",
                                       "The number of mesh subdivisions length-wise");
    mOptions["rows"] = Scene::Option("rows", "20",
                                      "The number of mesh subdisivisions width-wise");
    mOptions["buffer-usage"] = Scene::Option("buffer-usage", "static",
                                      "How the buffer will be used [static,stream,dynamic]");
}

SceneBuffer::~SceneBuffer()
{
    delete priv_;
}

int
SceneBuffer::load()
{
    mRunning = false;

    return 1;
}

void
SceneBuffer::unload()
{
}

void
SceneBuffer::setup()
{
    using LibMatrix::vec3;

    Scene::setup();

    bool interleave = (mOptions["interleave"].value == "true");
    Mesh::VBOUpdateMethod update_method;
    Mesh::VBOUsage usage;
    double update_fraction;
    double update_dispersion;
    size_t nlength;
    size_t nwidth;

    if (mOptions["update-method"].value == "map")
        update_method = Mesh::VBOUpdateMethodMap;
    else if (mOptions["update-method"].value == "subdata")
        update_method = Mesh::VBOUpdateMethodSubData;
    else
        update_method = Mesh::VBOUpdateMethodMap;

    if (mOptions["buffer-usage"].value == "static")
        usage = Mesh::VBOUsageStatic;
    else if (mOptions["buffer-usage"].value == "stream")
        usage = Mesh::VBOUsageStream;
    else
        usage = Mesh::VBOUsageDynamic;

    std::stringstream ss;
    ss << mOptions["update-fraction"].value;
    ss >> update_fraction;
    ss.clear();
    ss << mOptions["update-dispersion"].value;
    ss >> update_dispersion;
    ss.clear();
    ss << mOptions["columns"].value;
    ss >> nlength;
    ss.clear();
    ss << mOptions["rows"].value;
    ss >> nwidth;

    if (update_method == Mesh::VBOUpdateMethodMap &&
        (GLExtensions::MapBuffer == 0 || GLExtensions::UnmapBuffer == 0))
    {
        Log::error("Requested MapBuffer VBO update method but GL_OES_mapbuffer"
                   "is not supported!");
        return;
    }

    priv_->wave = new WaveMesh(5.0, 2.0, nlength, nwidth,
                               update_fraction * (1.0 - update_dispersion + 0.0001),
                               update_fraction);

    priv_->wave->mesh().interleave(interleave);
    priv_->wave->mesh().vbo_update_method(update_method);
    priv_->wave->mesh().vbo_usage(usage);
    priv_->wave->mesh().build_vbo();

    priv_->wave->program().start();
    priv_->wave->program()["Viewport"] = LibMatrix::vec2(mCanvas.width(), mCanvas.height());

    glDisable(GL_CULL_FACE);

    mCurrentFrame = 0;
    mRunning = true;
    mStartTime = Scene::get_timestamp_us() / 1000000.0;
    mLastUpdateTime = mStartTime;
}

void
SceneBuffer::teardown()
{
    priv_->wave->reset();

    glEnable(GL_CULL_FACE);

    Scene::teardown();
}

void
SceneBuffer::update()
{
    double current_time = Scene::get_timestamp_us() / 1000000.0;
    double elapsed_time = current_time - mStartTime;

    mLastUpdateTime = current_time;

    if (elapsed_time >= mDuration) {
        mAverageFPS = mCurrentFrame / elapsed_time;
        mRunning = false;
    }

    priv_->wave->update(elapsed_time);

    mCurrentFrame++;
}

void
SceneBuffer::draw()
{
    LibMatrix::Stack4 model_view;

    // Load the ModelViewProjectionMatrix uniform in the shader
    LibMatrix::mat4 model_view_proj(mCanvas.projection());
    model_view.translate(0.0, 0.0, -4.0);
    model_view.rotate(45.0, -1.0, -0.0, -0.0);
    model_view_proj *= model_view.getCurrent();

    priv_->wave->program()["ModelViewProjectionMatrix"] = model_view_proj;

    // Load the NormalMatrix uniform in the shader. The NormalMatrix is the
    // inverse transpose of the model view matrix.
    LibMatrix::mat4 normal_matrix(model_view.getCurrent());
    normal_matrix.inverse().transpose();
    priv_->wave->program()["NormalMatrix"] = normal_matrix;

    priv_->wave->mesh().render_vbo();
}

Scene::ValidationResult
SceneBuffer::validate()
{
    return Scene::ValidationUnknown;
}
