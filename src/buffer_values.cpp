#include <GL/glew.h>

#include "buffer_values.hpp"
#include "stage.hpp"

using namespace std;

BufferValues::~BufferValues() {
    glDeleteTextures(1, &text_tex);
    glDeleteBuffers(1, &text_vbo);
}

bool BufferValues::initialize() {
    if(FT_Init_FreeType(&ft)) {
        std::cerr << "Failed to initialize freetype" << std::endl;
        return false;
    }

    // The macro FONT_PATH is defined at compile time.
    if(FT_New_Face(ft, FONT_PATH, 0, &font)) {
        std::cerr << "Could not open font " FONT_PATH << std::endl;
        return false;
    }
    FT_Set_Pixel_Sizes(font, 0, font_size);

    text_prog.create(shader::text_vert_shader,
                     shader::text_frag_shader,
                     ShaderProgram::FormatR, {
                         "mvp",
                         "buff_sampler",
                         "text_sampler",
                         "pix_coord",
                         "brightness_contrast"
                     });

    glGenTextures(1, &text_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenBuffers(1, &text_vbo);
    generate_glyphs_texture();

    return true;
}

void BufferValues::generate_glyphs_texture() {
    const char text[]="0123456789., -enaninf";
    const unsigned char *p;
    const int border_size = 2;

    FT_GlyphSlot g = font->glyph;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text_tex);

    // Compute text box size
    float boxW = 0, boxH = 0;
    for(p = reinterpret_cast<const unsigned char*>(text); *p; p++) {
        if(FT_Load_Char(font, *p, FT_LOAD_RENDER))
            continue;
        text_texture_advances[*p][0] = (g->advance.x >> 6);
        text_texture_advances[*p][1] = (g->advance.y >> 6);
        text_texture_sizes[*p][0] = g->bitmap.width;
        text_texture_sizes[*p][1] = g->bitmap.rows;
        text_texture_tls[*p][0] = g->bitmap_left;
        text_texture_tls[*p][1] = g->bitmap_top;
        boxW += g->bitmap.width + 2*border_size;
        boxH = std::max(boxH, (float)g->bitmap.rows + 2*border_size);
    }

    text_texture_width = text_texture_height = 1.0f;
    while(text_texture_width<boxW) text_texture_width *= 2.f;
    while(text_texture_height<boxH) text_texture_height *= 2.f;

    const int mipmapLevels = 5;
    glTexStorage2D(GL_TEXTURE_2D, mipmapLevels, GL_R8, text_texture_width,
                   text_texture_height);

    // Clears generated buffer
    {
        std::vector<uint8_t> zeros(text_texture_width*text_texture_height, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, text_texture_width,
                        text_texture_height, GL_RED, GL_UNSIGNED_BYTE, zeros.data());
    }

    int x = 0, y = 0;
    for(p = reinterpret_cast<const unsigned char*>(text); *p; p++) {
        if(FT_Load_Char(font, *p, FT_LOAD_RENDER))
            continue;

        glTexSubImage2D(GL_TEXTURE_2D, 0, x+border_size, y + border_size,
                        g->bitmap.width, g->bitmap.rows,
                        GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
        text_texture_offsets[*p][0] = x + border_size;
        text_texture_offsets[*p][1] = y + border_size;

        x += g->bitmap.width + border_size*2;
        y += (g->advance.y >> 6);
    }

    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
}

void BufferValues::draw(const mat4& projection, const mat4& viewInv) {
    float zoom = (stage->getComponent<Camera>("camera_component"))->zoom;

    if(zoom > 40) {
        Buffer* buffer_component = stage->getComponent<Buffer>("buffer_component");
        float buffer_width_f = buffer_component->buffer_width_f;
        float buffer_height_f = buffer_component->buffer_height_f;
        int step = buffer_component->step;
        int channels = buffer_component->channels;
        Buffer::BufferType type = buffer_component->type;
        uint8_t* buffer = buffer_component->buffer;

        vec4 tl_ndc(-1,1,0,1);
        vec4 br_ndc(1,-1,0,1);
        mat4 vpInv = (projection*viewInv).inv();
        vec4 tl = vpInv*tl_ndc;
        vec4 br = vpInv*br_ndc;

        int lower_x = clamp(truncf(tl.x), 0.f, buffer_width_f-1.f);
        int upper_x = clamp(ceilf(br.x)+1.f, 1.f, buffer_width_f);

        int lower_y = clamp(truncf(tl.y), 0.f, buffer_height_f-1.0f);
        int upper_y = clamp(ceilf(br.y)+1.f, 1.f, buffer_height_f);

        char pix_label[30];
        for(int y = lower_y; y < upper_y; ++y) {
            for(int x = lower_x; x < upper_x; ++x) {
                int pos = (y*step + x)*channels;
                if(channels == 1) {
                    if(type == Buffer::BufferType::Float32) {
                        float fpix = reinterpret_cast<float*>(buffer)[pos];
                        sprintf(pix_label, "%.3f", fpix);
                        if(strlen(pix_label)>7)
                            sprintf(pix_label, "%.2e", fpix);
                    }
                    else if(type == Buffer::BufferType::UnsignedByte) {
                        sprintf(pix_label, "%d", buffer[pos]);
                    }
                    else if(type == Buffer::BufferType::Short) {
                        short fpix = reinterpret_cast<short*>(buffer)[pos];
                        sprintf(pix_label, "%d", fpix);
                    }
                    else if(type == Buffer::BufferType::UnsignedShort) {
                        short fpix = reinterpret_cast<unsigned short*>(buffer)[pos];
                        sprintf(pix_label, "%d", fpix);
                    }
                    else if(type == Buffer::BufferType::Int32) {
                        short fpix = reinterpret_cast<int*>(buffer)[pos];
                        sprintf(pix_label, "%d", fpix);
                    }
                    else if(type == Buffer::BufferType::UnsignedInt32) {
                        short fpix = reinterpret_cast<unsigned int*>(buffer)[pos];
                        sprintf(pix_label, "%d", fpix);
                    }

                    draw_text(projection, viewInv, pix_label, x, y, 0.0);
                } else if(channels==2) {
                    for(int c = 0; c < 2; ++c) {
                        float y_off = ((float)-c+0.5f)/3.0;
                        if(type == Buffer::BufferType::Float32) {
                            sprintf(pix_label, "%.4f",
                                    reinterpret_cast<float*>(buffer)[pos+c]);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedByte) {
                            sprintf(pix_label, "%d", buffer[pos+c]);
                            draw_text(projection, viewInv, pix_label, x, y, y_off, 0.5f);
                        }
                        else if(type == Buffer::BufferType::Short) {
                            short fpix = reinterpret_cast<short*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedShort) {
                            short fpix = reinterpret_cast<unsigned short*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::Int32) {
                            short fpix = reinterpret_cast<int*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedInt32) {
                            short fpix = reinterpret_cast<unsigned int*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                    }
                } else if(channels==3) {
                    for(int c = 0; c < 3; ++c) {
                        float y_off = ((float)-c+1.0f)/5.0;
                        if(type == Buffer::BufferType::Float32) {
                            sprintf(pix_label, "%.4f",
                                    reinterpret_cast<float*>(buffer)[pos+c]);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedByte) {
                            sprintf(pix_label, "%d", buffer[pos+c]);
                            draw_text(projection, viewInv, pix_label, x, y, y_off, 0.5f);
                        }
                        else if(type == Buffer::BufferType::Short) {
                            short fpix = reinterpret_cast<short*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedShort) {
                            short fpix = reinterpret_cast<unsigned short*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::Int32) {
                            short fpix = reinterpret_cast<int*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedInt32) {
                            short fpix = reinterpret_cast<unsigned int*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                    }
                } else if(channels==4) {
                    for(int c = 0; c < 4; ++c) {
                        float y_off = ((float)-c+1.5f)/4.0;
                        if(type == Buffer::BufferType::Float32) {
                            sprintf(pix_label, "%.4f",
                                    reinterpret_cast<float*>(buffer)[pos+c]);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedByte) {
                            sprintf(pix_label, "%d", buffer[pos+c]);
                            draw_text(projection, viewInv, pix_label, x, y, y_off, 0.5f);
                        }
                        else if(type == Buffer::BufferType::Short) {
                            short fpix = reinterpret_cast<short*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedShort) {
                            short fpix = reinterpret_cast<unsigned short*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::Int32) {
                            short fpix = reinterpret_cast<int*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                        else if(type == Buffer::BufferType::UnsignedInt32) {
                            short fpix = reinterpret_cast<unsigned int*>(buffer)[pos+c];
                            sprintf(pix_label, "%d", fpix);
                            draw_text(projection, viewInv, pix_label, x, y, y_off);
                        }
                    }
                }
            }
        }
    }
}

void BufferValues::draw_text(const mat4& projection, const mat4& viewInv,
        const char* text, float x, float y, float y_offset, float scale) {
    Buffer* buffer_component = stage->getComponent<Buffer>("buffer_component");
    const float* auto_buffer_contrast_brightness;
    if(stage->contrast_enabled) {
        auto_buffer_contrast_brightness =
                buffer_component->auto_buffer_contrast_brightness();
    } else {
        auto_buffer_contrast_brightness = Buffer::no_ac_params;
    }

    text_prog.use();
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

    glActiveTexture(GL_TEXTURE0);
    GLuint buff_tex = buffer_component->sub_texture_id_at_coord(x, y);
    glBindTexture(GL_TEXTURE_2D, buff_tex);
    text_prog.uniform1i("buff_sampler", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, text_tex);
    text_prog.uniform1i("text_sampler", 1);

    text_prog.uniformMatrix4fv("mvp", 1, GL_FALSE,
            (projection*viewInv).data);
    text_prog.uniform2f("pix_coord",
            buffer_component->tile_coord_x(x),
            buffer_component->tile_coord_y(y));

    text_prog.uniform3fv("brightness_contrast", 2,
            auto_buffer_contrast_brightness);

    // Compute text box size
    float boxW = 0, boxH = 0;
    for(const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; p++) {
        boxW += text_texture_advances[*p][0];
        boxH = max(boxH, (float)text_texture_sizes[*p][1]);
    }

    text_pixel_scale = max(text_pixel_scale, boxW*2.f/scale);
    float sx = 1.0/text_pixel_scale;
    float sy = 1.0/text_pixel_scale;

    y += 0.5 + boxH/2.0*sy - y_offset;
    x += 0.5 - boxW/2.0*sx;

    for(const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; p++) {
        float x2 = x + text_texture_tls[*p][0] * sx;
        float y2 = y - text_texture_tls[*p][1] * sy;

        int tex_wid = text_texture_sizes[*p][0];
        int tex_hei = text_texture_sizes[*p][1];
        float w = tex_wid * sx;
        float h = tex_hei * sy;

        float tex_lower_x = ((float)text_texture_offsets[*p][0])/text_texture_width;
        float tex_lower_y = ((float)text_texture_offsets[*p][1])/text_texture_height;
        float tex_upper_x = tex_lower_x + ((float)tex_wid-1.0f)/text_texture_width;
        float tex_upper_y = tex_lower_y + ((float)tex_hei-1.0f)/text_texture_height;

        GLfloat box[4][4] = {
            {x2,     y2    , tex_lower_x, tex_lower_y},
            {x2 + w, y2    , tex_upper_x, tex_lower_y},
            {x2,     y2 + h, tex_lower_x, tex_upper_y},
            {x2 + w, y2 + h, tex_upper_x, tex_upper_y},
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof box, box, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        x += text_texture_advances[*p][0] * sx;
        y += text_texture_advances[*p][1] * sy;
    }
}
