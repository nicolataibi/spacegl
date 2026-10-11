void drawText3D(float x, float y, float z, const char* text) {
    glRasterPos3f(x, y, z);
    for(size_t i=0; i<strlen(text); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, text[i]);
}

void getFactionColor(int f, float* r, float* g, float* b) {
    *r = 1.0; *g = 1.0; *b = 1.0; /* Default White */
    switch(f) {
        case 0:  *r = 0.4; *g = 0.7; *b = 1.0; break; /* Alliance Blue */
        case 10: *r = 1.0; *g = 0.1; *b = 0.0; break; /* Korthian Red */
        case 11: *r = 0.0; *g = 1.0; *b = 0.2; break; /* Xylari Green */
        case 12: *r = 0.0; *g = 0.8; *b = 0.8; break; /* Swarm Cyan */
        case 13: *r = 1.0; *g = 0.8; *b = 0.0; break; /* Vesperian Gold */
        case 14: *r = 0.8; *g = 0.0; *b = 1.0; break; /* Ascendant Purple */
        case 15: *r = 0.5; *g = 0.5; *b = 1.0; break; /* Quarzite Blue */
        case 16: *r = 0.8; *g = 0.4; *b = 0.0; break; /* Saurian Orange */
        case 17: *r = 1.0; *g = 1.0; *b = 0.0; break; /* Gilded Yellow */
        case 18: *r = 0.3; *g = 1.0; *b = 0.3; break; /* Fluidic Green */
        case 19: *r = 0.0; *g = 0.5; *b = 1.0; break; /* Cryos Deep Blue */
        case 20: *r = 1.0; *g = 0.5; *b = 0.5; break; /* Apex Salmon */
    }
}

const char* getSpeciesName(int s) {
    switch(s) {
        case 1: return "Player"; case 3: return "Starbase"; case 4: return "Star"; case 5: return "Planet"; case 6: return "Black Hole";
        case 7: return "Tactical Cruiser"; case 8: return "Pulsar";
        case 10: return "Korthian"; case 11: return "Xylari"; case 12: return "Swarm";
        case 13: return "Vesperian"; case 14: return "Ascendant"; case 15: return "Quarzite";
        case 16: return "Saurian"; case 17: return "Gilded"; case 18: return "Fluidic Void";
        case 19: return "Cryos"; case 20: return "Apex";
        default: return "Unknown";
    }
}

const char* getFactionHUDName(int f) {
    switch(f) {
        case 0:  return "Alliance";
        case 10: return "Korthian Empire";
        case 11: return "Xylari Star Empire";
        case 12: return "Swarm Collective";
        case 13: return "Vesperian Union";
        case 14: return "The Ascendancy";
        case 15: return "Quarzite Matrix";
        case 16: return "Saurian Legion";
        case 17: return "Gilded Cartel";
        case 18: return "Fluidic Void";
        case 19: return "Cryos Enclave";
        case 20: return "Apex Stalkers";
        default: return "Unknown Faction";
    }
}

const char* getClassName(int c) {
    switch(c) {
        case SHIP_CLASS_LEGACY: return "Legacy Class";
        case SHIP_CLASS_SCOUT:      return "Scout Class";
        case SHIP_CLASS_HEAVY_CRUISER:    return "Heavy Cruiser";
        case SHIP_CLASS_MULTI_ENGINE: return "Multi-Engine Cruiser";
        case SHIP_CLASS_ESCORT:      return "Escort Class";
        case SHIP_CLASS_EXPLORER:       return "Explorer Class";
        case SHIP_CLASS_FLAGSHIP:    return "Flagship Class";
        case SHIP_CLASS_SCIENCE:     return "Science Vessel";
        case SHIP_CLASS_CARRIER:        return "Carrier Class";
        case SHIP_CLASS_TACTICAL:       return "Tactical Cruiser";
        case SHIP_CLASS_DIPLOMATIC:   return "Diplomatic Cruiser";
        case SHIP_CLASS_RESEARCH:       return "Research Vessel";
        case SHIP_CLASS_FRIGATE:  return "Frigate Class";
        case SHIP_CLASS_SENTINEL: return "Sentinel Class";
        case SHIP_CLASS_GENERIC_ALIEN: return "Vessel";
        default:                      return "Vessel";
    }
}

void drawHUD(int obj_idx) {
    if (obj_idx < 0 || obj_idx >= MAX_OBJECTS) return;
    GameObject *obj = &objects[obj_idx];
    if (obj->type == 28) return; /* Skip HUD labels for Torpedoes */
    float x = obj->x;
    float y = obj->y;
    float z = obj->z;
    int type = obj->type;
    int id = obj->id;
    int hp = obj->health_pct;

    GLdouble winX, winY, winZ;

    /* Project slightly above the ship (Y is height in viewer) */
    float v_off = (type == 21) ? 0.4 : 0.8;
    if (gluProject(x, y + v_off, z, hud_model, hud_proj, hud_view, &winX, &winY, &winZ) == GL_TRUE) {
        /* Check if behind camera or too close to clip plane */
        if (winZ < 0.0 || winZ > 1.0) return;

        glMatrixMode(GL_PROJECTION);
        glPushMatrix(); glLoadIdentity();
        gluOrtho2D(0, hud_view[2], 0, hud_view[3]);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);

        /* Draw Name/ID - Use a fixed offset from winY to keep it above health bar */
        char buf[128];
        bool handled_custom = false;
        if (type == 1) {
            /* Player: Faction - Class (Captain) */
            if (obj->faction == 0) { // FACTION_ALLIANCE
                sprintf(buf, "Alliance - %s (%s)", getClassName(obj->ship_class), obj->name);
            } else {
                sprintf(buf, "%s (%s)", getFactionHUDName(obj->faction), obj->name);
            }
            glColor3f(0.0, 1.0, 1.0); /* Cyan for player */
        } else if (type >= 10 && type < 21) {
            /* NPC: Species [ID] */
            if (type == 12) { /* Swarm Special */
                sprintf(buf, "Swarm CUBE [%d]", id);
                glColor3f(0.0, 1.0, 0.0); /* Green for Swarm */
            } else {
                sprintf(buf, "%s [%d]", (obj->name[0] != '\0') ? obj->name : getSpeciesName(type), id);
                glColor3f(1.0, 0.3, 0.3); /* Redish for NPCs */
            }
        } else {
            /* Other: Name based on type */
            const char* type_name = "Object";
            switch(type) {
                case 3: type_name = "STARBASE"; glColor3f(0.0, 1.0, 0.0); break;
                case 4: {
                    type_name = "STAR"; glColor3f(1.0, 1.0, 0.0);
                    const char* classes[] = {"O (Blue)", "B (Light Blue)", "A (White)", "F (Yellow-White)", "G (Yellow)", "K (Orange)", "M (Red)"};
                    int c_idx = obj->ship_class; if(c_idx<0) c_idx=0; if(c_idx>6) c_idx=6;
                    sprintf(buf, "STAR: %s [%d]", classes[c_idx], id);
                    handled_custom = true;
                } break;
                case 5: {
                    type_name = "PLANET"; glColor3f(0.0, 1.0, 0.5);
                    const char* res[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
                    int r_idx = obj->ship_class; if(r_idx<0) r_idx=0; if(r_idx>8) r_idx=8;
                    sprintf(buf, "PLANET: %s [%d]", res[r_idx], id);
                    handled_custom = true;
                } break;
                case 6: type_name = "BLACK HOLE"; glColor3f(0.5, 0.0, 1.0); break;
                case 7: {
                    type_name = "NEBULA"; glColor3f(0.7, 0.7, 0.7);
                    const char* neb_classes[] = {"Standard Class", "High-Energy Class", "Dark Matter Class", "Ionic Class", "Gravimetric Class", "Temporal Class"};
                    int n_idx = obj->ship_class; if(n_idx<0) n_idx=0; if(n_idx>5) n_idx=5;
                    sprintf(buf, "NEBULA: %s [%d]", neb_classes[n_idx], id);
                    handled_custom = true;
                } break;
                case 8: type_name = "PULSAR"; glColor3f(1.0, 0.5, 0.0); break;
                case 9: type_name = "COMET"; glColor3f(0.5, 0.8, 1.0); break;
                case 21: {
                    type_name = "ASTEROID"; glColor3f(0.6, 0.4, 0.2); 
                    const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
                    int r_idx = obj->ship_class; if(r_idx<0) r_idx=0; if(r_idx>8) r_idx=8;
                    sprintf(buf, "ASTEROID [%d]: %s (%" PRIu64 " units)", id, res_names[r_idx], obj->energy);
                    handled_custom = true;
                } break;
                case 22: type_name = "DERELICT"; glColor3f(0.4, 0.4, 0.4); break;
                case 23: type_name = "MINE"; glColor3f(1.0, 0.0, 0.0); break;
                case 24: type_name = "BUOY"; glColor3f(0.0, 0.5, 1.0); break;
                case 25: type_name = "PLATFORM"; glColor3f(1.0, 0.6, 0.0); break;
                case 26: type_name = "RIFT"; glColor3f(0.0, 1.0, 1.0); break;
                case 29: {
                    type_name = "QUASAR"; glColor3f(1.0, 0.0, 1.0);
                    const char* q_classes[] = {"Radio-loud", "Radio-quiet", "Broad Abs-Line", "Type 2", "Red Quasar", "Optically Violent", "Weak Emission"};
                    int q_idx = obj->ship_class; if(q_idx<0) q_idx=0; if(q_idx>6) q_idx=6;
                    sprintf(buf, "QUASAR: %s [%d]", q_classes[q_idx], id);
                    handled_custom = true;
                } break;
                case 30: 
                case 31: {
                    type_name = (type == 30) ? "CRYSTALLINE ENTITY" : "SPACE AMOEBA";
                    glColor3f(1.0, 1.0, 1.0);
                    sprintf(buf, "%s [%d]", type_name, id);
                } break;
            }
            if (handled_custom) {
                /* Skip generic formatting */
            } else if (obj->name[0] != '\0' && strcmp(obj->name, "Unknown") != 0) {
                sprintf(buf, "%s: %s [%d]", type_name, obj->name, id);
            } else {
                sprintf(buf, "%s [%d]", type_name, id);
            }
        }
        
        glRasterPos2f(winX - (strlen(buf)*4), winY + 25);
        for(size_t i=0; i<strlen(buf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, buf[i]);

        /* Draw Health Bar (Ships, Bases, Platforms, Monsters) */
        if (type == 1 || type == 3 || (type >= 10 && type <= 20) || type == 22 || type == 25 || type >= 30) {
            double w = QUADRANT_SIZE;
            double h = 4.0;
            double bar = (hp / (double)YIELD_HARVEST_MAX) * w;
            if (bar < 0) bar = 0; 
            if (bar > w) bar = w;

            /* Border */
            glColor3f(0.5, 0.5, 0.5);
            glBegin(GL_LINE_LOOP);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX + w/2, winY);
            glVertex2f(winX + w/2, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();

            /* Fill */
            if (hp > (int)THRESHOLD_SYS_DEGRADED) glColor3f(0.0, 1.0, 0.0);
            else if (hp > (int)THRESHOLD_SYS_DAMAGED) glColor3f(1.0, 1.0, 0.0);
            else glColor3f(1.0, 0.0, 0.0);

            glBegin(GL_QUADS);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX - w/2 + bar, winY);
            glVertex2f(winX - w/2 + bar, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();

            /* Hull Integrity Text */
            char hbuf[32];
            sprintf(hbuf, "HULL: %d%%", obj->hull_integrity);
            if (obj->hull_integrity > 60) glColor3f(0, 1, 0);
            else if (obj->hull_integrity > 25) glColor3f(1, 1, 0);
            else glColor3f(1, 0, 0);
            glRasterPos2f(winX + w/2 + 5, winY);
            for(size_t i=0; i<strlen(hbuf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, hbuf[i]);

            /* Plating indicator if present */
            if (obj->plating > 0) {
                char pbuf[32];
                sprintf(pbuf, "HULL: +%d", obj->plating);
                glColor3f(1.0, 0.8, 0.0); /* Yellow for Composite */
                glRasterPos2f(winX - (strlen(pbuf)*4), winY + 35);
                for(size_t i=0; i<strlen(pbuf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, pbuf[i]);
            }
        }

        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }
}

void drawHeadingRing() {
    /* 1. Heading Ring (Horizontal plane XZ in OpenGL) */
    glColor4f(0.0, 1.0, 1.0, 0.3);
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<360; i+=5) {
        double rad = i * M_PI / 180.0;
        glVertex3f(sin(rad)*2.5, 0, cos(rad)*2.5);
    }
    glEnd();

    /* 2. Heading Labels (every 45 degrees) */
    glColor3f(0.0, 0.8, 0.8);
    for(int i=0; i<360; i+=45) {
        double rad = i * M_PI / 180.0;
        double lx = sin(rad)*2.7;
        double lz = cos(rad)*2.7;
        char buf[8]; sprintf(buf, "%d", i);
        drawText3D(lx, 0.1, lz, buf);
    }
}

void drawFixedCompass() {
    /* Fixed Horizontal Compass - White */
    glDisable(GL_LIGHTING);
    glColor4f(1.0, 1.0, 1.0, 0.4);
    
    /* Outer Ring */
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<360; i+=5) {
        double rad = i * M_PI / 180.0;
        glVertex3f(sin(rad)*3.0, 0, cos(rad)*3.0);
    }
    glEnd();

    /* Inner Ticks and Labels */
    glColor3f(0.9, 0.9, 0.9);
    for(int i=0; i<360; i+=30) {
        double rad = i * M_PI / 180.0;
        glBegin(GL_LINES);
        glVertex3f(sin(rad)*2.9, 0, cos(rad)*2.9);
        glVertex3f(sin(rad)*3.1, 0, cos(rad)*3.1);
        glEnd();
        
        if (i % 90 == 0) {
            double lx = sin(rad)*3.3;
            double lz = cos(rad)*3.3;
            const char* nsew[] = {"N", "E", "S", "W"}; /* Simplified N=0, E=90, S=180, W=270 */
            drawText3D(lx, 0.1, lz, nsew[i/90]);
        }
    }
}

void drawMarkArc() {
    /* 3. Mark Arc (Longitudinal Vertical plane - Aligned with ship nose) */
    glColor4f(1.0, 1.0, 0.0, 0.2);
    glBegin(GL_LINE_STRIP);
    for(int i=-90; i<=90; i+=5) {
        double rad = i * M_PI / 180.0;
        double vx = cos(rad) * 2.5;
        double vy = sin(rad) * 2.5;
        glVertex3f(vx, vy, 0);
    }
    glEnd();

    /* 4. Mark Labels */
    glColor3f(0.8, 0.8, 0.0);
    int marks[] = {-90, -45, 0, 45, 90};
    for(int i=0; i<5; i++) {
        double rad = marks[i] * M_PI / 180.0;
        double lx = cos(rad) * 2.8;
        double ly = sin(rad) * 2.8;
        char buf[16]; sprintf(buf, "M:%+d", marks[i]);
        drawText3D(lx, ly, 0, buf);
    }
}

void drawRollCircle() {
    /* 5. Roll Circle (Transversal Vertical plane - Perpendicular to nose) */
    glColor4f(1.0, 1.0, 0.0, 0.2);
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<360; i+=5) {
        double rad = i * M_PI / 180.0;
        double vy = sin(rad) * 2.5;
        double vz = cos(rad) * 2.5;
        glVertex3f(0, vy, vz);
    }
    glEnd();

    /* 6. Roll Labels */
    glColor3f(0.8, 0.8, 0.0);
    int rolls[] = {-90, -45, 0, 45, 90};
    for(int i=0; i<5; i++) {
        double rad = rolls[i] * M_PI / 180.0;
        double ly = sin(rad) * 2.8;
        double lz = cos(rad) * 2.8;
        char buf[16]; sprintf(buf, "R:%+d", rolls[i]);
        drawText3D(0, ly, lz, buf);
    }
}

void drawCompass() {
    glDisable(GL_LIGHTING);
    drawHeadingRing();
    glPushMatrix();
    glRotatef(objects[0].h, 0, 1, 0);
    glRotatef(-objects[0].m, 0, 0, 1); /* Pitch rotation (elevation) */
    drawMarkArc();
    drawRollCircle();
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawGlow(double radius, double r, double g, double b, double alpha) {
    if (g_is_cloaked_rendering) {
        return;
    }
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    for (int i = 1; i <= 5; i++) {
        double s = radius * (1.0 + i * 0.2);
        glColor4f(r, g, b, alpha / (i * 1.5));
        glutSolidSphere(s, 16, 16);
    }
    glPopAttrib();
}

void drawSentinelQuantumCore(float radius_base, float r, float g, float b) {
    if (g_is_cloaked_rendering) {
        return;
    }
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int r_idx = 0; r_idx < 3; r_idx++) {
        glPushMatrix();
        /* Specific Sentinel Rotation Pattern: inverted and faster */
        glRotatef(pulse * (60.0 + r_idx * 20.0), 1, 0, 0); 
        glRotatef(pulse * -20.0, 0, 1, 0);
        glColor4f(r, g, b, 0.7f - (r_idx * 0.15f));
        /* Robust rings for Heavy Escort */
        glutWireTorus(0.03, radius_base + (r_idx * 0.18), 12, 36);
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void glutSolidSphere_wrapper_module() { glutSolidSphere(0.5, 64, 64); }
void glutSolidSphere_wrapper_hull() { glutSolidSphere(0.15, 48, 48); }
void glutSolidSphere_wrapper_escort() { glutSolidSphere(0.35, 48, 48); }
void glutSolidCube_wrapper() { glutSolidCube(0.5); }

void glutOctagonalCylinder_wrapper() {
    double radius = 0.16;
    double height = 0.48;
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 8; i++) {
        double theta = (double)i * 2.0 * 3.14159265358979323846 / 8.0;
        double c = cos(theta);
        double s = sin(theta);
        glNormal3f(c, s, 0.0);
        glVertex3f(radius * c, radius * s, 0.0);
        glVertex3f(radius * c, radius * s, height);
    }
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0.0, 0.0, -1.0);
    glVertex3f(0.0, 0.0, 0.0);
    for (int i = 8; i >= 0; i--) {
        double theta = (double)i * 2.0 * 3.14159265358979323846 / 8.0;
        glVertex3f(radius * cos(theta), radius * sin(theta), 0.0);
    }
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0.0, 0.0, 1.0);
    glVertex3f(0.0, 0.0, height);
    for (int i = 0; i <= 8; i++) {
        double theta = (double)i * 2.0 * 3.14159265358979323846 / 8.0;
        glVertex3f(radius * cos(theta), radius * sin(theta), height);
    }
    glEnd();
}

void glutOctagonalCone_wrapper() {
    double radius = 0.16;
    double height = 0.28;
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0.0, 0.0, 1.0);
    glVertex3f(0.0, 0.0, height);
    for (int i = 0; i <= 8; i++) {
        double theta = (double)i * 2.0 * 3.14159265358979323846 / 8.0;
        double c = cos(theta);
        double s = sin(theta);
        double nz = radius / height;
        double len = sqrt(c*c + s*s + nz*nz);
        glNormal3f(c/len, s/len, nz/len);
        glVertex3f(radius * c, radius * s, 0.0);
    }
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0.0, 0.0, -1.0);
    glVertex3f(0.0, 0.0, 0.0);
    for (int i = 8; i >= 0; i--) {
        double theta = (double)i * 2.0 * 3.14159265358979323846 / 8.0;
        glVertex3f(radius * cos(theta), radius * sin(theta), 0.0);
    }
    glEnd();
}

void drawHullDetail(void (*drawFunc)(void), double r, double g, double b) {
    glShadeModel(GL_SMOOTH);
    if (g_is_derelict_rendering) {
        glColor3f(0.25, 0.25, 0.27);
    } else {
        glColor3f(r, g, b); 
    }
    drawFunc();
    /* Wireframe overlay removed to ensure homogeneous surface */
}

void drawNacelle(double len, double width, double r, double g, double b) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    /* Nacelle Body (Shaded - High tessellation for smooth look) */
    glPushMatrix(); 
    glScalef(len, width, width); 
    if (g_is_derelict_rendering) {
        glColor3f(0.2, 0.2, 0.22);
    } else {
        glColor3f(0.45, 0.45, 0.5); 
    }
    glutSolidSphere(0.1, 48, 32); 
    glPopMatrix();

    if (g_is_cloaked_rendering || g_is_derelict_rendering) {
        glPopAttrib();
        return;
    }

    /* Blue Hyperdrive Field Grille (Emissive) */
    GLfloat nac_emit[] = {r*0.7, g*0.7, b*0.7, 1.0};
    GLfloat no_emit[] = {0.0, 0.0, 0.0, 1.0};
    glPushMatrix(); 
    glTranslatef(-0.05 * len, 0, 0); 
    glScalef(len*0.6, width*1.1, width*1.1);
    glMaterialfv(GL_FRONT, GL_EMISSION, nac_emit);
    glColor3f(r, g, b);
    glutSolidSphere(0.08, 12, 12);
    glMaterialfv(GL_FRONT, GL_EMISSION, no_emit);
    glPopMatrix();

    /* Bussard Collector (Front Red Tip) - UNLIT for maximum brightness */
    glDisable(GL_LIGHTING);
    glPushMatrix(); 
    glTranslatef(0.1 * len, 0, 0); 
    glColor3f(1.0, 0.0, 0.0); /* Solid Bright Red */
    glutSolidSphere(0.05, 16, 16); 
    /* Extra Glow Layer */
    glEnable(GL_BLEND);
    glColor4f(1.0, 0.2, 0.0, 0.4);
    glutSolidSphere(0.07, 12, 12);
    glPopMatrix();
    glPopAttrib();
}

void drawDeflector(double r, double g, double b) {
    if (g_is_cloaked_rendering) {
        return;
    }
    glDisable(GL_LIGHTING);
    glColor3f(r, g, b);
    glutSolidSphere(0.12, 16, 16);
    glEnable(GL_LIGHTING);
}

void drawCommandModule(double sx, double sy, double sz) {
    glPushMatrix(); glScalef(sx, sy, sz); drawHullDetail(glutSolidSphere_wrapper_module, 0.88, 0.88, 0.92); glPopMatrix();
    
    if (g_is_cloaked_rendering) return;

    /* Upper navigation lights */
    glDisable(GL_LIGHTING);
    glColor3f(1.0, 0.0, 0.0); /* Red (Port) */
    glPushMatrix(); glTranslatef(0, 0.12, 0.2); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glColor3f(0.0, 1.0, 0.0); /* Green (Starboard) */
    glPushMatrix(); glTranslatef(0, 0.12, -0.2); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawLegacy() {
    /* --- [ NOMENCLATURE: LEGACY CLASS ] --- */
    
    /* 1. Primary Hull (Command Saucer) */
    drawCommandModule(1.0, 0.15, 1.0);
    
    /* 2. Bridge Module (Tactical Hub) */
    glDisable(GL_LIGHTING); 
    glColor3f(0.0, 0.5, 1.0); 
    glPushMatrix(); 
    glTranslatef(0, 0.1, 0); 
    glutSolidSphere(0.08, 12, 12); 
    drawGlow(0.06, 0, 0.5, 1, 0.4); 
    glPopMatrix(); 
    glEnable(GL_LIGHTING);
    
    /* 3. Secondary Hull (Engineering Section) */
    glPushMatrix(); 
    glTranslatef(-0.6, 0.0, 0); 
    glRotatef(180, 0, 1, 0);
    glScalef(2.16, 1.2, 1.2); 
    drawHullDetail(glutSolidSphere_wrapper_hull, 0.8, 0.8, 0.85); 
    glPopMatrix();

    /* 3.5 Stabilizing Rudder (Aetherium Fin) */
    glPushMatrix();
    glTranslatef(-0.9, 0.12, 0); 
    glScalef(0.65, 0.45, 0.03); 
    glColor3f(0.8, 0.8, 0.85);
    glutSolidSphere(0.5, 32, 16);
    glPopMatrix();

    /* 4. Main Deflector Array & Propulsion Rings */
    glPushMatrix(); 
    glTranslatef(-1.44, 0.0, 0); 
    glScalef(0.25, 0.25, 0.25); 
    drawDeflector(1.0, 0.4, 0.0); 
    drawGlow(0.1, 1.0, 0.3, 0.0, 0.5); 
    
    /* Original Legacy Rings Design */
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int r = 0; r < 3; r++) {
        glPushMatrix();
        glRotatef(pulse * (40.0 + r * 15.0), 1, 0, 0); 
        glRotatef(pulse * 10.0, 0, 1, 0);
        glColor4f(0.0, 0.6, 1.0, 0.6f - (r * 0.1f));
        glutWireTorus(0.01, 0.35 + (r * 0.12), 8, 32);
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();

    /* 5. Nacelles and Pylons */
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix(); 
        glTranslatef(-0.918, 0.0, side * 0.30); 
        glRotatef(side * 90, 1, 0, 0); 
        glScalef(0.38, 0.76, 0.076); 
        glColor3f(0.8, 0.8, 0.85); 
        glutSolidSphere(0.5, 24, 16); 
        glPopMatrix();

        glPushMatrix(); 
        glTranslatef(-1.02, 0.0, side * 0.65); 
        drawNacelle(4.8, 0.28, 0.2, 0.5, 1.0); 
        glPopMatrix();
    }
}

void drawScout() { 
    drawCommandModule(1.2, 0.18, 1.1);
    glPushMatrix(); 
    glTranslatef(-0.1, 0.0, 0); 
    glScalef(0.3, 0.5, 0.9); 
    glColor3f(0.75, 0.75, 0.8); 
    glutSolidCube(0.5); 
    glPopMatrix();
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix(); 
        glTranslatef(-0.1, 0.0, side * 0.3); 
        glScalef(0.1, 0.4, 0.1); 
        glutSolidCube(1.0); 
        glPopMatrix();
        
        glPushMatrix(); 
        glTranslatef(-0.2, 0.0, side * 0.45); 
        drawNacelle(3.8, 0.4, 0.3, 0.4, 0.8); 
        glPopMatrix();
    }
}

void drawHeavyCruiser() { 
    drawCommandModule(1.4, 0.12, 1.3);
    
    /* Secondary Hull - Integrated */
    glPushMatrix(); 
    glTranslatef(-0.5, 0.0, 0); 
    glRotatef(180, 0, 1, 0);
    glScalef(2.8, 0.7, 0.7); 
    drawHullDetail(glutSolidSphere_wrapper_hull, 0.8, 0.8, 0.85); 
    glPopMatrix();

    /* Deflector Dish */
    glPushMatrix(); 
    glTranslatef(-1.0, 0.0, 0); 
    drawDeflector(0.0, 0.6, 1.0); 
    drawGlow(0.1, 0.2, 0.7, 1.0, 0.4); 
    glPopMatrix();

    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix(); 
        glTranslatef(-0.5, 0.0, side * 0.35); 
        drawNacelle(5.5, 0.25, 0.2, 0.6, 1.0); 
        glPopMatrix();
    }
}

void drawMultiEngine() {
    glPushMatrix(); 
    glScalef(1.2, 0.4, 0.9); 
    drawHullDetail(glutSolidCube_wrapper, 0.75, 0.75, 0.8); 
    glPopMatrix();
    for (int updown = -1; updown <= 1; updown += 2) {
        for (int side = -1; side <= 1; side += 2) {
            glPushMatrix(); 
            glTranslatef(-0.1, updown * 0.1, side * 0.35); 
            drawNacelle(3.5, 0.3, 0.5, 0.5, 0.6); 
            glPopMatrix();
        }
    }
}

void drawEscort() {
    glPushMatrix(); 
    glTranslatef(0.1, 0, 0); 
    glScalef(0.5, 0.4, 0.4); 
    drawDeflector(1.0, 0.3, 0.0); 
    drawGlow(0.15, 1.0, 0.2, 0.0, 0.4); 
    glPopMatrix();
    
    glPushMatrix(); 
    glScalef(1.5, 0.5, 1.8); 
    drawHullDetail(glutSolidSphere_wrapper_escort, 0.6, 0.6, 0.65); 
    glPopMatrix();
    
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix(); 
        glTranslatef(-0.1, 0.0, side * 0.4); 
        drawNacelle(2.5, 0.6, 0.2, 0.3, 0.7); 
        glPopMatrix();
    }
}

void drawExplorer() {
    /* --- [ NOMENCLATURE: EXPLORER CLASS ] --- */

    /* 1. Primary Hull (Command Saucer) - Main Command Deck */
    glPushMatrix();
    drawCommandModule(1.6, 0.15, 2.4);
    
    if (!g_is_cloaked_rendering) {
        /* 2. Bridge Module (Command Hub) */
        glDisable(GL_LIGHTING);
        glColor3f(1.0, 1.0, 1.0);
        glPushMatrix(); glTranslatef(0, 0.15, 0); glScalef(0.3, 0.1, 0.2); glutSolidSphere(0.5, 12, 12); glPopMatrix();
        
        /* 3. External Window Grid */
        glColor3f(1.0, 1.0, 0.8);
        for(int i=0; i<360; i+=30) {
            glPushMatrix();
            glRotatef(i, 0, 1, 0);
            glTranslatef(1.5, 0, 0);
            glutSolidSphere(0.015, 4, 4);
            glPopMatrix();
        }

        /* 4. Multi-Spectral Sensor Probes */
        for(int i=0; i<3; i++) {
            glPushMatrix();
            glRotatef(pulse * ((float)GAME_TICK_RATE / 2.0) + (i * 120), 0, 1, 0); 
            glRotatef(((float)GAME_TICK_RATE / 2.0), 1, 0, 1);
            glTranslatef(2.2, 0, 0);
            glColor3f(0.0, 0.7, 1.0); 
            glutSolidSphere(0.03, 8, 8);
            glPopMatrix();
        }
        glEnable(GL_LIGHTING);
    }
    glPopMatrix();

    /* 5. Structural Neck (REMOVED for integrated design) */

    /* 6. Secondary Hull (Engineering) - Integrated & Inverted & Widened & Lengthened */
    glPushMatrix();
    glTranslatef(-0.3, 0.0, 0); 
    glRotatef(180, 0, 1, 0); 
    glScalef(2.64, 1.2, 1.35); /* Lengthened by 20% (2.2 -> 2.64) */
    drawHullDetail(glutSolidSphere_wrapper_hull, 0.85, 0.85, 0.9);
    glPopMatrix();

    /* 7. Advanced Main Deflector Dish */
    if (!g_is_cloaked_rendering) {
        glPushMatrix();
        glTranslatef(-0.7, 0.0, 0); 
        glRotatef(-90, 0, 1, 0);
        glRotatef(90, 0, 0, 1); 
        
        /* Deflector Housing */
        glColor3f(0.6, 0.4, 0.2);
        glutSolidTorus(0.05, 0.25, 8, 24);
        
        /* Resonance Dish */
        glPushMatrix();
        glScalef(0.1, 1.0, 1.0);
        glColor3f(0.0, 0.2, 0.5);
        glutSolidSphere(0.22, 16, 16);
        glPopMatrix();

        /* Deflector Core (GDIS Data Stream) */
        glDisable(GL_LIGHTING);
        double pulse_glow = 0.8 + sin(pulse*3.0)*0.2;
        glColor3f(0.0, 0.6 * pulse_glow, 1.0 * pulse_glow);
        glutSolidSphere(0.08, 12, 12);
        glEnable(GL_LIGHTING);
        glPopMatrix();
    }

    /* 8. Nacelle Pylons (Structural Support) */
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix();
        glTranslatef(-0.3, 0.0, side * 0.6);
        glRotatef(side * 90, 1, 0, 0); 
        glScalef(0.1, 0.6, 0.02); 
        glColor3f(0.8, 0.8, 0.82);
        glutSolidCube(1.0);
        glPopMatrix();

        /* 9. Hyperdrive Nacelles (FTL Propulsion) */
        glPushMatrix();
        glTranslatef(-0.3, 0.0, side * 1.2);
        drawNacelle(4.5, 0.35, 0.2, 0.6, 1.0);
        glPopMatrix();
    }
}

void drawFlagship() {
    drawCommandModule(2.2, 0.12, 1.3);
    glPushMatrix(); glTranslatef(-0.5, 0.0, 0); glRotatef(180, 0, 1, 0); glScalef(2.5, 0.5, 0.6); drawHullDetail(glutSolidSphere_wrapper_hull, 0.9, 0.9, 0.95); glPopMatrix();
    glPushMatrix(); glTranslatef(-1.0, 0.0, 0); drawDeflector(0.0, 0.4, 0.8); drawGlow(0.1, 0.2, 0.6, 1.0, 0.4); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.5, 0.0, side * 0.45); drawNacelle(6.0, 0.2, 0.2, 0.5, 1.0); glPopMatrix();
    }
}

void drawScience() {
    drawCommandModule(2.0, 0.15, 1.0);
    glPushMatrix(); glTranslatef(-0.4, 0.0, 0); glRotatef(180, 0, 1, 0); glScalef(1.8, 0.4, 0.5); drawHullDetail(glutSolidSphere_wrapper_hull, 0.85, 0.85, 0.95); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.8, 0.0, 0); drawDeflector(0.0, 0.5, 0.9); drawGlow(0.1, 0.3, 0.7, 1.0, 0.4); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.4, 0.0, side * 0.4); glRotatef(side*90, 1, 0, 0);
        drawNacelle(3.5, 0.25, 0.3, 0.6, 1.0); glPopMatrix();
    }
}

void drawCarrier() {
    glColor3f(0.6, 0.6, 0.7); glPushMatrix(); glScalef(1.4, 0.2, 1.8); drawHullDetail(glutSolidSphere_wrapper_module, 0.6, 0.6, 0.7); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.6, -0.2, side * 0.7); drawNacelle(4.5, 0.4, 0.4, 0.4, 0.8); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.2, -0.1, side * 0.5); glRotatef(side*30, 1,0,0); glScalef(0.2, 0.6, 0.2); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawTactical() {
    drawExplorer();
    glPushMatrix(); glTranslatef(-0.6, 0.4, 0); glColor3f(0.7, 0.7, 0.75);
    glPushMatrix(); glScalef(0.8, 0.15, 0.8); glutSolidSphere(0.5, 24, 24); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.2, 0); glScalef(0.1, 0.4, 0.4); glutSolidCube(1.0); glPopMatrix();
    glPopMatrix();
}

void drawDiplomatic() {
    drawCommandModule(1.4, 0.2, 1.4);
    glPushMatrix(); glTranslatef(-0.45, -0.25, 0); glScalef(1.6, 0.8, 0.8); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8, 0.8, 0.85); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.15, -0.25, 0); drawDeflector(0.0, 0.3, 0.7); drawGlow(0.1, 0.2, 0.5, 1.0, 0.4); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7, 0.05, side * 0.45); drawNacelle(4.5, 0.35, 0.3, 0.4, 0.9); glPopMatrix();
    }
}

void drawResearch() {
    glColor3f(0.9, 0.9, 0.9); glPushMatrix(); glScalef(1.1, 0.15, 0.9); drawHullDetail(glutSolidSphere_wrapper_module, 0.9, 0.9, 0.9); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.5, 0); glScalef(1.2, 0.3, 0.6); glutSolidSphere(0.2, 12, 12); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(0, -0.25, side * 0.35); glScalef(0.15, 0.6, 0.1); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.35, -0.25, side * 0.4); drawNacelle(2.5, 0.2, 0.4, 0.4, 0.7); glPopMatrix();
    }
}

void drawFrigate() {
    glColor3f(0.6, 0.6, 0.65); glPushMatrix(); glScalef(1.6, 0.25, 1.5); drawHullDetail(glutSolidSphere_wrapper_module, 0.6, 0.6, 0.65); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7, -0.05, side * 0.55); drawNacelle(3.5, 0.3, 0.2, 0.3, 0.6); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.8, 0, 0); glScalef(0.1, 0.1, 1.0); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawSentinel() {
    /* --- [ NOMENCLATURE: SENTINEL CLASS ] --- */
    /* Heavy Defensive Escort - Octagonal tapered design with Quad-Propulsion */
    
    glPushMatrix();
    glRotatef(90.0, 1.0, 0.0, 0.0); /* Rotate the entire ship 90 degrees around X-axis */

    /* 1. Main Command Hull (Octagonal Cylinder) */
    glPushMatrix();
    glTranslatef(-0.48, 0.0, 0.0); /* Original -0.32 * 1.5 for lengthening */
    glScalef(1.5, 0.5, 0.5);       /* Lengthen by 50% (1.5), Thin by 50% (0.5) */
    glRotatef(90.0, 0.0, 1.0, 0.0);
    drawHullDetail(glutOctagonalCylinder_wrapper, 0.65, 0.65, 0.72);
    glPopMatrix();
    
    /* 1.5 Tapered Front Hull (Octagonal Cone) */
    glPushMatrix();
    glTranslatef(0.24, 0.0, 0.0); /* Original 0.16 * 1.5 for lengthening */
    glScalef(1.5, 0.5, 0.5);      /* Lengthen by 50% (1.5), Thin by 50% (0.5) */
    glRotatef(90.0, 0.0, 1.0, 0.0);
    drawHullDetail(glutOctagonalCone_wrapper, 0.7, 0.7, 0.77);
    glPopMatrix();
    
    /* 2. Tactical Bridges (Dual Redundant) - REMOVED per request */
    
    /* 3. Quad Propulsion System & Angled Pylons */
    for (int side = -1; side <= 1; side += 2) {
        for (int updown = -1; updown <= 1; updown += 2) {
            /* Perpendicular pylons (90 degrees to hull) pointing to target positions */
            glPushMatrix();
            /* Align X with nacelle (-0.3) */
            glTranslatef(-0.3, 0.0, 0.0);
            
            /* Keep pylons pointing to original target (updown*0.12, side*0.28) */
            double angle_rad = atan2(side * 0.28, updown * 0.12);
            double angle_deg = angle_rad * 180.0 / 3.1415926535;
            
            glRotatef(angle_deg, 1.0, 0.0, 0.0); /* Rotate in YZ plane to target */
            glRotatef(90.0, 0.0, 1.0, 0.0);      /* Rotate to point outward (perpendicular to X) */
            
            /* Move outward to bridge the gap */
            glTranslatef(0.19, 0.0, 0.0); 
            
            /* Scale reduced by 20% (1.2 -> 0.96) */
            glScalef(0.96, 0.06, 0.06); 
            drawHullDetail(glutSolidSphere_wrapper_hull, 0.55, 0.55, 0.6);
            glPopMatrix();

            /* The 4 separate Engine Nacelles (Rotated 90 degrees around hull axis) */
            glPushMatrix();
            /* Swap coordinates to achieve 90 deg rotation: (updown*0.12, side*0.28) rotated 90 deg */
            /* Using (side*0.28, -updown*0.12) or similar to match the pylon's target intersection */
            glTranslatef(-0.3, side * 0.28, -updown * 0.12);
            drawNacelle(3.2, 0.60, 0.0, 0.8, 1.0); /* Reduced length by 60% from 8.0 to 3.2 */
            glPopMatrix();
        }
    }
    
    /* 4. Quantum Propulsion Core (Rear Mounted) */
    glPushMatrix();
    glTranslatef(-0.6, 0.0, 0.0);
    drawDeflector(0.0, 1.0, 1.0);
    drawSentinelQuantumCore(0.22, 0.0, 0.8, 1.0);
    glPopMatrix();

    glPopMatrix(); /* End of global ship rotation */
}

void drawAllianceShip(int class_idx, double h, double m, double r) {
    if (class_idx < 0 || class_idx > 13) {
        class_idx = 0;
    }
    if (!g_is_cloaked_rendering) {
        glUseProgram(hullShaderProgram);
    }
    glPushMatrix();
    glRotatef(h - 90.0, 0, 1, 0);
    glRotatef(m, 0, 0, 1);
    glRotatef(r, 1, 0, 0); /* Longitudinal Roll */
    switch (class_idx) {
        case SHIP_CLASS_LEGACY:
            drawLegacy();
            break;
        case SHIP_CLASS_SCOUT:
            drawScout();
            break;
        case SHIP_CLASS_HEAVY_CRUISER:
            drawHeavyCruiser();
            break;
        case SHIP_CLASS_MULTI_ENGINE:
            drawMultiEngine();
            break;
        case SHIP_CLASS_ESCORT:
            drawEscort();
            break;
        case SHIP_CLASS_EXPLORER:
            drawExplorer();
            break;
        case SHIP_CLASS_FLAGSHIP:
            drawFlagship();
            break;
        case SHIP_CLASS_SCIENCE:
            drawScience();
            break;
        case SHIP_CLASS_CARRIER:
            drawCarrier();
            break;
        case SHIP_CLASS_TACTICAL:
            drawTactical();
            break;
        case SHIP_CLASS_DIPLOMATIC:
            drawDiplomatic();
            break;
        case SHIP_CLASS_RESEARCH:
            drawResearch();
            break;
        case SHIP_CLASS_FRIGATE:
            drawFrigate();
            break;
        case SHIP_CLASS_SENTINEL:
            drawSentinel();
            break;
        default:
            drawLegacy();
            break;
    }
    glPopMatrix();
    glUseProgram(0);
}

void drawKorthian(double x, double y, double z) { (void)x; (void)y; (void)z;
    if (g_is_derelict_rendering) glColor3f(0.25, 0.25, 0.27); else glColor3f(0.6, 0.1, 0.0);
    glPushMatrix(); glScalef(1.0, 0.3, 1.5); glutSolidSphere(0.3, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4, 0, 0); glScalef(2.0, 0.2, 0.2); glutSolidSphere(0.15, 8, 8); glPopMatrix();
    if (g_is_derelict_rendering) glColor3f(0.3, 0.3, 0.32); else glColor3f(0.8, 0.0, 0.0);
    glPushMatrix(); glTranslatef(0.7, 0, 0); glScalef(1.0, 0.5, 1.2); glutSolidSphere(0.15, 12, 12); glPopMatrix();
}

void drawXylari(double x, double y, double z) { (void)x; (void)y; (void)z;
    if (g_is_derelict_rendering) glColor3f(0.2, 0.25, 0.2); else glColor3f(0.0, 0.5, 0.0);
    glPushMatrix(); glScalef(1.5, 0.2, 1.0); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.25, 0); glScalef(1.5, 0.2, 0.8); glutSolidSphere(0.35, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4, 0.1, 0); glScalef(1.0, 0.5, 0.2); glutSolidCube(0.3); glPopMatrix();
    if (g_is_derelict_rendering) glColor3f(0.25, 0.3, 0.25); else glColor3f(0.0, 0.7, 0.2);
    glPushMatrix(); glTranslatef(0.7, 0.1, 0); glutSolidCone(0.1, 0.3, 8, 8); glPopMatrix();
}

void drawSwarm(double x, double y, double z) { (void)x; (void)y; (void)z;
    glRotatef(pulse*5, 1, 1, 1); glColor3f(0.15, 0.15, 0.15); glutWireCube(0.85);
    glColor3f(0.05, 0.05, 0.05); glutSolidCube(0.75); glDisable(GL_LIGHTING);
    double p = (sin(pulse)+1.0)*0.5; glColor4f(0.0, 0.8 * p, 0.0, 0.6); glutWireCube(0.8);
    for(int i=0; i<6; i++) {
        glPushMatrix(); if(i==0) glTranslatef(0.38,0,0); else if(i==1) glTranslatef(-0.38,0,0); else if(i==2) glTranslatef(0,0.38,0); else if(i==3) glTranslatef(0,-0.38,0); else if(i==4) glTranslatef(0,0,0.38); else if(i==5) glTranslatef(0,0,-0.38);
        glColor3f(0, 1, 0); glutSolidSphere(0.04, 8, 8); drawGlow(0.03, 0, 1, 0, 0.4); glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

void drawVesperian(double x, double y, double z) { (void)x; (void)y; (void)z;
    glColor3f(0.6, 0.5, 0.3); glPushMatrix(); glScalef(2.0, 0.2, 1.2); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glColor3f(0.8, 0.7, 0.2); glPushMatrix(); glTranslatef(0.5, 0, 0); glScalef(1.0, 0.4, 0.4); glutSolidSphere(0.2, 12, 12); glPopMatrix();
}

void drawAscendant(double x, double y, double z) { (void)x; (void)y; (void)z;
    glColor3f(0.4, 0.4, 0.6); glPushMatrix(); glScalef(1.2, 0.5, 1.0); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4, 0, 0.15); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4, 0, -0.15); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
}

void drawQuarzite(double x, double y, double z) { (void)x; (void)y; (void)z;
    glRotatef(pulse*15, 0, 1, 0); glColor4f(1.0, 0.5, 0.0, 0.6); glDisable(GL_LIGHTING); glutWireOctahedron();
    glColor4f(1.0, 0.2, 0.0, 0.4); glutSolidOctahedron(); glEnable(GL_LIGHTING);
}

void drawSaurian(double x, double y, double z) { (void)x; (void)y; (void)z;
    glColor3f(0.3, 0.4, 0.1); glPushMatrix(); glScalef(1.5, 0.6, 0.6); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3, 0, 0); glScalef(0.5, 1.2, 1.5); glutSolidCube(0.3); glPopMatrix();
}

void drawGilded(double x, double y, double z) { (void)x; (void)y; (void)z;
    glColor3f(0.7, 0.3, 0.1); glPushMatrix(); glScalef(1.0, 0.2, 2.0); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.3, 0, 0); glScalef(1.2, 0.4, 0.6); glutSolidSphere(0.3, 12, 12); glPopMatrix();
}

void drawFluidicVoid(double x, double y, double z) { (void)x; (void)y; (void)z;
    glRotatef(pulse*10, 1, 0, 1); glColor3f(0.8, 0.8, 0.2); for(int i=0; i<3; i++) { glPushMatrix(); glRotatef(i*120, 0, 1, 0); glTranslatef(0.3, 0, 0); glScalef(2.0, 0.3, 0.3); glutSolidSphere(0.15, 12, 12); glPopMatrix(); }
    glutSolidSphere(0.2, 12, 12);
}

void drawCryos(double x, double y, double z) { (void)x; (void)y; (void)z;
    glColor3f(0.4, 0.5, 0.4); glPushMatrix(); glScalef(1.8, 0.2, 0.8); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(0.2, 0.1, 0.2); glScalef(0.5, 0.5, 1.2); glutSolidSphere(0.2, 8, 8); glPopMatrix();
}

void drawApex(double x, double y, double z) { (void)x; (void)y; (void)z;
    glColor3f(0.5, 0.5, 0.5); glPushMatrix(); glScalef(2.5, 0.15, 0.4); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.4, 0, 0); glScalef(0.5, 0.8, 1.5); glutSolidCube(0.2); glPopMatrix();
}

void drawStarbase(double x, double y, double z, int faction) { (void)x; (void)y; (void)z;
    if (faction == 10) { /* Korthian */
        glColor3f(0.15, 0.15, 0.15); glutSolidCube(1.2);
        glColor3f(1.0, 0.1, 0.0);
        for(int i=0; i<4; i++) {
            glPushMatrix(); glRotatef(i*90.0 + pulse*30.0, 0,1,0); glTranslatef(0,0,1.2); glScalef(0.2, 0.2, 3.5); glutSolidCube(1.0); glPopMatrix();
        }
    } else if (faction == 11) { /* Xylari */
        glColor4f(0.0, 1.0, 0.2, 0.8);
        for(int i=0; i<3; i++) {
            glPushMatrix(); glTranslatef(0, (i-1)*1.0, 0); double s = 1.2 + 0.4*sin(pulse*2.0 + i); glutWireSphere(s, 16, 16); glPopMatrix();
        }
    } else if (faction == 12) { /* Swarm */
        glColor3f(0.1, 0.1, 0.1);
        for(int i=0; i<8; i++) {
            glPushMatrix(); glTranslatef(sin(i)*1.5, cos(i)*1.5, sin(i*2.0)*1.5); glRotatef(pulse*30.0 + i*50.0, 1,1,1); glutSolidCube(0.8); glPopMatrix();
        }
    } else if (faction == 13) { /* Vesperian */
        glColor3f(0.8, 0.8, 0.9);
        for(int i=-1; i<=1; i+=2) {
            glPushMatrix(); glTranslatef(i*1.2, 0, 0); glScalef(0.4, 4.0, 0.4); glutSolidCube(1.0); glPopMatrix();
        }
        glColor4f(1.0, 0.0, 1.0, 0.8);
        for(int i=-1; i<=1; i++) {
            glPushMatrix(); glTranslatef(0, i*2.0, 0); glRotatef(90, 1,0,0); glRotatef(pulse*120.0, 0,0,1); glutWireTorus(0.05, 2.0, 8, 32); glPopMatrix();
        }
    } else if (faction == 14) { /* Ascendant */
        glColor3f(0.8, 0.8, 1.0); glutSolidSphere(1.0, 24, 24);
        glColor4f(0.5, 0.0, 0.8, 0.8);
        for(int i=0; i<3; i++) {
            glPushMatrix(); glRotatef(pulse*60.0*(1.0+i), 1,1,0); glutWireTorus(0.05, 1.8+i*0.6, 8, 32); glPopMatrix();
        }
    } else if (faction == 15) { /* Quarzite */
        glColor3f(1.0, 0.5, 0.0);
        for(int i=0; i<4; i++) {
            glPushMatrix(); glRotatef(pulse*12.0 + i*45.0, 0,1,1); glScalef(1.5, 0.4, 1.5); glutSolidCube(1.0); glPopMatrix();
        }
    } else if (faction == 16) { /* Saurian */
        glColor3f(0.4, 0.4, 0.3); glutSolidCube(2.0);
        glColor3f(0.4, 0.6, 0.1);
        for(int i=-1; i<=1; i+=2) {
            glPushMatrix(); glTranslatef(0, i*1.0, 0); glScalef(2.1, 0.3, 2.1); glutSolidCube(1.0); glPopMatrix();
        }
    } else if (faction == 17) { /* Gilded */
        glColor3f(1.0, 0.8, 0.1); glutSolidSphere(1.5, 24, 24);
        for(int i=0; i<3; i++) {
            glPushMatrix(); glRotatef(90, 1,0,0); glRotatef(pulse*30.0*(i+1), 0,0,1); glutWireTorus(0.1, 1.8+i*0.5, 8, 32); glPopMatrix();
        }
    } else if (faction == 18) { /* Fluidic Void */
        glColor4f(0.7, 1.0, 0.0, 0.6);
        for(int i=0; i<3; i++) {
            glPushMatrix(); glRotatef(pulse*60.0*(1.0+i), 0.5, 1, 0); double s = 1.5 + 0.5*sin(pulse*3.0 + i); glScalef(s, s*0.8, s); glutWireSphere(1.0, 16, 16); glPopMatrix();
        }
    } else if (faction == 19) { /* Cryos */
        glColor4f(0.0, 0.8, 1.0, 0.8);
        for(int i=0; i<5; i++) {
            glPushMatrix(); glRotatef(i*72.0 + pulse*12.0, 0,1,0); glTranslatef(0,0,1.2); glScalef(0.3, 2.5+sin(i), 0.3); glutSolidCube(1.0); glPopMatrix();
        }
    } else if (faction == 20) { /* Apex */
        glColor3f(0.2, 0.0, 0.0); glPushMatrix(); glScalef(1.2, 3.5, 1.2); glutSolidCube(1.0); glPopMatrix();
        glColor3f(1.0, 0.1, 0.1); glPushMatrix(); glScalef(1.3, 0.15, 1.3); glutSolidCube(1.0); glPopMatrix();
    } else { /* Default / Alliance */
        glPushMatrix(); glRotatef(pulse*30.0, 0, 1, 0); glColor3f(1.0, 0.8, 0.0); double s = 0.8 + 0.2*sin(pulse*3.0); glutWireSphere(s, 16, 16); glPopMatrix();
        glColor3f(0.3, 0.3, 0.3); glPushMatrix(); glScalef(0.2, 4.0, 0.2); glutSolidCube(1.0); glPopMatrix();
        glColor3f(0.5, 0.5, 0.5); glPushMatrix(); glRotatef(pulse*12.0, 0,1,0); glScalef(2.2, 0.15, 2.2); glutSolidCube(1.0); glPopMatrix();
        glColor3f(0.6, 0.6, 0.6);
        float offsets[] = {1.2, -1.2};
        for(int i=0; i<2; i++) { glPushMatrix(); glTranslatef(0, offsets[i], 0); glRotatef(-pulse*24.0*(i+1), 0,1,0); glScalef(1.0, 0.08, 1.0); glutSolidCube(1.0); glPopMatrix(); }
    }
}

void drawStar(double x, double y, double z, int id) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);

    /* Spectral Class Color based on ID */
    double r = 1.0, g = 1.0, b = 1.0;
    int type = id % 7;
    if (type == 0) { r=0.4; g=0.6; b=1.0; }      /* Class O - Blue */
    else if (type == 1) { r=0.7; g=0.8; b=1.0; } /* Class B - Light Blue */
    else if (type == 2) { r=1.0; g=1.0; b=1.0; } /* Class A - White */
    else if (type == 3) { r=1.0; g=1.0; b=0.7; } /* Class F - Yellow-White */
    else if (type == 4) { r=1.0; g=0.9; b=0.1; } /* Class G - Yellow (Sol) */
    else if (type == 5) { r=1.0; g=0.6; b=0.2; } /* Class K - Orange */
                        else { r=1.0; g=0.2; b=0.1; }                /* Class H - Red */
    /* 3. Core */
    double core_size = 0.38;
    
    /* Violent pulsation if this is the supernova star */
    /* Check using the world coordinates passed to the function AND global quadrant */
    if (g_sn_pos.active && g_sn_q[0] == g_my_q[0] && g_sn_q[1] == g_my_q[1] && g_sn_q[2] == g_my_q[2] &&
        fabs(x - g_sn_pos.x) < 0.1 && fabs(y - g_sn_pos.y) < 0.1 && fabs(z - g_sn_pos.z) < 0.1) {
        double sn_factor = 1.0 - (g_sn_pos.timer / 1800.0); /* 0 to 1 as it nears explosion */
        core_size += sn_factor * 0.4 * (sin(pulse*30.0)*0.5 + 0.5);
        glColor3f(1.0, 1.0, 1.0); /* Turning white hot */
    } else {
        glColor3f(r, g, b);
    }
    glutSolidSphere(core_size, 32, 32);

    /* 2. ATMOSPHERIC LAYERS (Corona): Semi-transparent and glowing */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive glow */
    glDepthMask(GL_FALSE); /* Aura doesn't block depth */

    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(pulse * (10.0 + i*5.0), 0, 1, 0);
        glRotatef(pulse * (5.0 + i*2.0), 1, 0, 0);
        double s = 0.4 + i*0.12 + sin(pulse*3.0 + i)*0.03;
        glColor4f(r, g, b, 0.3 / (i+1));
        glutSolidSphere(s, 16, 16);
        glPopMatrix();
    }
    
    /* 3. Solar Flares (Prominences) */
    glLineWidth(1.0);
    for(int i=0; i<8; i++) {
        glPushMatrix();
        glRotatef(i*45 + pulse*20, 0, 1, 0);
        glRotatef(sin(pulse + i)*30, 0, 0, 1);
        double flare_len = 0.45 + sin(pulse*5.0 + i)*0.2;
        glBegin(GL_LINES);
        glColor4f(r, g, b, 0.7);
        glVertex3f(0.3, 0, 0);
        glColor4f(r, g, b, 0.0);
        glVertex3f(flare_len, 0, 0);
        glEnd();
        glPopMatrix();
    }

    glPopAttrib();
}

void drawPlanet(double x, double y, double z) { (void)x; (void)y; (void)z;
    GLfloat spec[] = {0.3, 0.3, 0.3, 1.0};
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec);
    glMateriali(GL_FRONT, GL_SHININESS, 50);
    
    glColor3f(0.2, 0.6, 1.0);
    glutSolidSphere(0.3, 24, 24);
    
    /* Atmosphere */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0, 1.0, 1.0, 0.2);
    glutSolidSphere(0.32, 24, 24);
    glDisable(GL_BLEND);
}

void drawWormhole_v1(double x, double y, double z, double h, double m, int type) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(h - 90.0, 0, 1, 0);
    glRotatef(m, 0, 0, 1);
    glRotatef(90, 0, 1, 0); /* Align longitudinal axis with ship forward */

    /* 1. Draw the central 3D sphere with material properties */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    
    if (type == 0) {
        /* DEPARTURE: Dark charcoal with metallic specularity */
        float mat_amb[] = {0.0, 0.0, 0.0, 1.0};
        float mat_diff[] = {0.02, 0.02, 0.02, 1.0};
        float mat_spec[] = {0.6, 0.6, 0.6, 1.0};
        glMaterialfv(GL_FRONT, GL_AMBIENT, mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diff);
        glMaterialfv(GL_FRONT, GL_SPECULAR, mat_spec);
        glMaterialf(GL_FRONT, GL_SHININESS, 100.0);
        glColor3f(0.05, 0.05, 0.05);
    } else {
        /* ARRIVAL: Brilliant White/Blue Singularity */
        float mat_amb[] = {0.8, 0.8, 1.0, 1.0};
        float mat_diff[] = {1.0, 1.0, 1.0, 1.0};
        float mat_spec[] = {1.0, 1.0, 1.0, 1.0};
        glMaterialfv(GL_FRONT, GL_AMBIENT, mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diff);
        glMaterialfv(GL_FRONT, GL_SPECULAR, mat_spec);
        glMaterialf(GL_FRONT, GL_SHININESS, 128.0);
        glColor3f(1.0, 1.0, 1.0);
    }
    
    glutSolidSphere(0.35, 32, 32);
    glDisable(GL_LIGHTING);

    /* 2. Activate shader for the external energy cones */
    if (whShaderProgram) {
        glUseProgram(whShaderProgram);
        glUniform1f(glGetUniformLocation(whShaderProgram, "time"), pulse);
    }

    glRotatef(pulse * 25.0, 0, 0, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); 
    glDisable(GL_DEPTH_TEST); 
    
    double rs = 0.45;
    double rmax = 1.6;
    int Nr = 15;
    int Nt = 30;
    double dr = (rmax - rs) / Nr;
    double dth = 2.0 * M_PI / Nt;

    for (int side = -1; side <= 1; side += 2) {
        for(int i=0; i<Nr; i++){
            double r = rs + i * dr;
            double base_z = 0.35 + 2.0 * sqrt(rs * (r - rs));
            double zz = (type != 0) ? (2.39 - base_z) : base_z;
            
            double fade = 1.0 - (double)i/Nr;
            if (type == 0) {
                glColor4f(0.3*fade, 0.0, 1.0*fade, 0.8*fade); 
            } else {
                glColor4f(1.0*fade, 0.8*fade, 0.2*fade, 0.8*fade);      
            }
            glBegin(GL_LINE_LOOP);
            for(int j=0; j<=Nt; j++){
                double th = (double)j * dth;
                glVertex3f(r * cos(th), r * sin(th), side * zz);
            }
            glEnd();
        }
        for(int j=0; j<Nt; j++){
            double th = (double)j * dth;
            glBegin(GL_LINE_STRIP);
            for(int i=0; i<Nr; i++){
                double r = rs + i * dr;
                double base_z = 0.35 + 2.0 * sqrt(rs * (r - rs));
                double zz = (type != 0) ? (2.39 - base_z) : base_z;
                
                double fade = 1.0 - (double)i/Nr;
                if (type == 0) {
                    glColor4f(0.2*fade, 0.0, 0.6*fade, 0.6*fade);
                } else {
                    glColor4f(0.8*fade, 0.6*fade, 0.2*fade, 0.6*fade);
                }
                glVertex3f(r * cos(th), r * sin(th), side * zz);
            }
            glEnd();
        }
    }
    glPopAttrib();
    glPopMatrix();
    glUseProgram(0);
}

void drawWormhole_v2(double x, double y, double z, double h, double m, int type) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushMatrix();
    glTranslatef(x, y, z);

    /* World orientation — same convention as ship directional vector:
       Step 1: Heading rotation around Y (h - 90 offset aligns forward to +X)
       Step 2: Mark (pitch) rotation around Z */
    glRotatef(h - 90.0, 0, 1, 0);
    glRotatef(m, 0, 0, 1);

    /* --- Cinematic cyclic funnel colors --- */
    /* Use three out-of-phase oscillators for rich hue cycling */
    float r_c = 0.5f + 0.5f * sinf(pulse * 1.3f);
    float g_c = 0.5f + 0.5f * sinf(pulse * 1.3f + 2.094f); /* +120 deg */
    float b_c = 0.5f + 0.5f * sinf(pulse * 1.3f + 4.189f); /* +240 deg */

    /* === PART 1: ROTATING WIREFRAME CUBE OUTER SHELL === */
    glDisable(GL_LIGHTING);
    glPushMatrix();
    glRotatef(pulse * 20.0, 1, 1, 1);
    glColor3f(0.25, 0.25, 0.25);
    glPushMatrix();
    glScalef(1.7, 1.7, 1.7);
    glutWireCube(1.0);
    glPopMatrix();
    glPopMatrix();

    /* === PART 2: CENTRAL SINGULARITY SPHERE === */
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    float mat_spec[] = {1.0f, 1.0f, 1.0f, 1.0f};
    float mat_emit_zero[] = {0.0f, 0.0f, 0.0f, 1.0f};
    glMaterialfv(GL_FRONT, GL_SPECULAR, mat_spec);
    glMaterialf(GL_FRONT, GL_SHININESS, 128.0f);

    if (type == 1) {
        /* Arrival: Brilliant White — use emission so it glows under any lighting */
        float mat_amb[]  = {1.0f, 1.0f, 1.0f, 1.0f};
        float mat_diff[] = {1.0f, 1.0f, 1.0f, 1.0f};
        float mat_emit[] = {0.8f, 0.8f, 0.8f, 1.0f};
        glMaterialfv(GL_FRONT, GL_AMBIENT,   mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE,   mat_diff);
        glMaterialfv(GL_FRONT, GL_EMISSION,  mat_emit);
        glColor3f(1.0f, 1.0f, 1.0f);
    } else {
        /* Departure: Solid Black — zero everything including emission */
        float mat_amb[]  = {0.0f, 0.0f, 0.0f, 1.0f};
        float mat_diff[] = {0.0f, 0.0f, 0.0f, 1.0f};
        glMaterialfv(GL_FRONT, GL_AMBIENT,   mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE,   mat_diff);
        glMaterialfv(GL_FRONT, GL_EMISSION,  mat_emit_zero);
        glColor3f(0.0f, 0.0f, 0.0f);
    }
    glutSolidSphere(0.7, 32, 32);
    /* Reset emission after drawing sphere */
    glMaterialfv(GL_FRONT, GL_EMISSION, mat_emit_zero);

    glDisable(GL_LIGHTING);

    /* === PART 3: ARRIVAL — EXTRA VOLUMETRIC GLOW === */
    if (type == 1) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        struct { float r, g, b, a, s; } layers[] = {
            {1.0, 1.0, 1.0, 0.8, 1.0},
            {0.0, 0.5, 1.0, 0.5, 1.6},
            {0.2, 0.0, 0.8, 0.3, 2.2},
            {0.0, 0.0, 0.4, 0.15, 3.0}
        };
        for (int i = 0; i < 4; i++) {
            glColor4f(layers[i].r, layers[i].g, layers[i].b, layers[i].a);
            glutSolidSphere(layers[i].s, 16, 16);
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    /* === PART 4: ENERGY FUNNELS (fixed-function, no shader) === */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glUseProgram(0); /* Ensure no shader overrides glColor4f */
    glLineWidth(2.0);

    /* Rotate the funnels on the longitudinal axis */
    glPushMatrix();
    glRotatef(pulse * 35.0, 0, 0, 1);

    double rs   = 0.7;  /* Start at sphere surface */
    double rmax = 2.2;  /* Max funnel radius */
    int    Nr   = 18;
    int    Nt   = 32;
    double dr   = (rmax - rs) / (Nr - 1);
    double dth  = 2.0 * M_PI / Nt;

    for (int side = -1; side <= 1; side += 2) {
        /* Funnel cyclic color */
        if (type == 0) {
            /* Departure: Purple/Blue cycle */
            glColor4f(0.3 + 0.5 * r_c, 0.05 * g_c, 0.5 + 0.5 * b_c, 0.9);
        } else {
            /* Arrival: Cyan/White cycle */
            glColor4f(0.4 + 0.6 * r_c, 0.6 + 0.4 * g_c, 0.8 + 0.2 * b_c, 0.9);
        }

        /* Ring segments */
        for (int i = 0; i < Nr; i++) {
            double r  = rs + i * dr;
            double zz = 2.0 * sqrt(rs * (r - rs));

            double fade = 1.0 - (double)i / Nr;
            if (type == 0) {
                /* Departure: warm cycle — red/orange/violet/magenta */
                float rr = (0.6f + 0.4f * r_c) * fade;
                float gr = (0.1f * g_c)         * fade;
                float br = (0.4f + 0.4f * b_c)  * fade;
                glColor4f(rr, gr, br, 0.9f * fade);
            } else {
                /* Arrival: cool cycle — electric blue/cyan/gold */
                float rr = (0.1f + 0.7f * r_c)  * fade;
                float gr = (0.4f + 0.5f * g_c)  * fade;
                float br = (0.6f + 0.4f * b_c)  * fade;
                glColor4f(rr, gr, br, 0.9f * fade);
            }

            glBegin(GL_LINE_LOOP);
            for (int j = 0; j < Nt; j++) {
                double th = j * dth;
                glVertex3f(r * cos(th), r * sin(th), side * zz);
            }
            glEnd();
        }

        /* Spoke segments */
        for (int j = 0; j < Nt; j += 2) {
            double th = j * dth;
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < Nr; i++) {
                double r  = rs + i * dr;
                double zz = 2.0 * sqrt(rs * (r - rs));
                double fade = 1.0 - (double)i / Nr;
                if (type == 0) {
                    /* Departure spokes: darker warm tones */
                    float rr = (0.4f + 0.4f * r_c) * fade;
                    float gr = (0.05f * g_c)        * fade;
                    float br = (0.3f + 0.3f * b_c)  * fade;
                    glColor4f(rr, gr, br, 0.7f * fade);
                } else {
                    /* Arrival spokes: cool tones */
                    float rr = (0.05f + 0.6f * r_c) * fade;
                    float gr = (0.3f  + 0.5f * g_c) * fade;
                    float br = (0.5f  + 0.4f * b_c) * fade;
                    glColor4f(rr, gr, br, 0.7f * fade);
                }
                glVertex3f(r * cos(th), r * sin(th), side * zz);
            }
            glEnd();
        }
    }

    glPopMatrix(); /* funnel rotation */

    glPopAttrib();
    glPopMatrix();
    glUseProgram(0);
}

void drawWormhole(double x, double y, double z, double h, double m, int type, int ver) {
    if (ver == 1) drawWormhole_v1(x, y, z, h, m, type);
    else drawWormhole_v2(x, y, z, h, m, type);
}

void drawBlackHole(double x, double y, double z) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE); /* Don't write to depth buffer to avoid square artifacts */
    
    glPushMatrix();
    glTranslatef(x, y, z);
    
    /* Billboard logic: face the camera */
    float m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (i == j) {
                m[i * 4 + j] = 1.0;
            } else {
                m[i * 4 + j] = 0.0;
            }
        }
    }
    glLoadMatrixf(m);

    glUseProgram(bhShaderProgram);
    glUniform1f(glGetUniformLocation(bhShaderProgram, "time"), pulse);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_scene);
    glUniform1i(glGetUniformLocation(bhShaderProgram, "sceneTex"), 0);

    /* Draw full-size quad for the shader to work on */
    double s = 1.2;
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex3f(-s, -s, 0);
    glTexCoord2f(1,0); glVertex3f( s, -s, 0);
    glTexCoord2f(1,1); glVertex3f( s,  s, 0);
    glTexCoord2f(0,1); glVertex3f(-s,  s, 0);
    glEnd();

    glUseProgram(0);
    glPopMatrix();
    glPopAttrib();
}

void drawStellarNebula(double x, double y, double z, int type) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* Multilayered gas cloud */
    for (int i = 0; i < 5; i++) {
        glPushMatrix();
        glRotatef(pulse * (3.0 + i * 1.5), 0, 1, 0);
        glRotatef(pulse * (2.0 + i), 1, 0, 0);
        
        double scale = 3.0 + i * 0.8;
        double alpha = 0.2 - (i * 0.03);
        if (alpha < 0.02) alpha = 0.02;
        
        if (type == 1) { /* High-Energy: Yellowish-Orange */
            glColor4f(0.8, 0.5, 0.1, alpha);
        } else if (type == 2) { /* Dark Matter: Dark Grey/Black with faint Purple */
            glColor4f(0.1, 0.05, 0.2, alpha);
        } else if (type == 3) { /* Diffuse: White/Blue */
            glColor4f(0.8, 0.8, 0.9, alpha);
        } else if (type == 4) { /* Planetary/SNR: Green-Cyan */
            glColor4f(0.1, 0.6, 0.4, alpha);
        } else if (type == 5) { /* GMC: Orange/Rust */
            glColor4f(1.0, 0.4, 0.2, alpha);
        } else { /* Standard (Default): Purple and Blue */
            glColor4f(0.4, 0.2, 0.6, alpha);
        }
        
        glScalef(scale, scale * 0.8, scale * 1.2);
        glutSolidSphere(1.0, 16, 16);
        glPopMatrix();
    }
    
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawProbe1() {
    /* Small pulsing effect - tripled base size (0.15) */
    double time = glutGet(GLUT_ELAPSED_TIME) / 1000.0;
    double s = 0.15 + sin(time * 10.0) * 0.03;
    glScalef(s, s, s);
    
    glColor3f(0.0, 1.0, 1.0);
    glutWireCube(1.0);
    
    /* Glowing core */
    glColor4f(0.0, 0.8, 1.0, 0.5);
    glutSolidCube(0.5);
}

void drawProbe2() {
    double time = glutGet(GLUT_ELAPSED_TIME) / 1000.0;
    double s = 0.15 + sin(time * 8.0) * 0.02;
    glScalef(s, s, s);

    /* Central Sphere */
    glColor3f(0.0, 1.0, 0.5);
    glutSolidSphere(0.4, 16, 16);

    /* 3 Rotating Rings */
    glColor3f(0.0, 0.8, 1.0);
    for(int r=0; r<3; r++) {
        glPushMatrix();
        glRotatef(time * 100.0 + (r * 60.0), (r==0), (r==1), (r==2));
        glutWireTorus(0.02, 0.8, 8, 32);
        glPopMatrix();
    }
}

void drawProbe3() {
    double time = glutGet(GLUT_ELAPSED_TIME) / 1000.0;
    double s = 0.15 + sin(time * 6.0) * 0.02;
    glScalef(s, s, s);

    /* Central Sphere */
    glColor3f(1.0, 0.5, 0.0);
    glutSolidSphere(0.4, 16, 16);

    /* 3 Rotating Squares (Wireframe Cubes) */
    glColor3f(1.0, 1.0, 0.0);
    for(int q=0; q<3; q++) {
        glPushMatrix();
        glRotatef(time * 80.0 + (q * 120.0), (q==0), (q==1), (q==2));
        glTranslatef(0.8, 0, 0);
        glutWireCube(0.3);
        glPopMatrix();
    }
}

void drawProbe(int p_idx, double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    
    if (p_idx == 0) {
        drawProbe1();
    } else if (p_idx == 1) {
        drawProbe2();
    } else {
        drawProbe3();
    }
    
    glPopMatrix();
}

void drawInterstellarFilament() {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glPushMatrix();
    glRotatef(pulse * 20.0, 0, 1, 0);
    glBegin(GL_LINES);

    /* Draw twisting electrical lines */
    for(int i=0; i<3; i++) {
        float offsetPhase = pulse * 2.0f + i * 2.09f;
        for(float y=-2.0f; y<=2.0f; y+=0.1f) {
            float x = sin(y * 4.0 + offsetPhase) * 0.3;
            float z = cos(y * 5.0 + offsetPhase) * 0.3;

            float intensity = (sin(y * 10.0 - pulse * 10.0) + 1.0) * 0.5;
            glColor4f(0.2f + intensity*0.8f, 0.6f, 1.0f, 0.8f);

            glVertex3f(x, y, z);

            float nx = sin((y+0.1) * 4.0 + offsetPhase) * 0.3;
            float nz = cos((y+0.1) * 5.0 + offsetPhase) * 0.3;
            glVertex3f(nx, y+0.1, nz);
        }
    }
    glEnd();
    glPopMatrix();

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawPulsar(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    /* Core */
    glColor3f(1.0, 1.0, 1.0);
    glutSolidSphere(0.2, 16, 16);
    
    /* Beams */
    glPushMatrix();
    glRotatef(pulse * 100.0, 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_LINES);
    for (int i = -1; i <= 1; i += 2) {
        glColor4f(1.0, 0.5, 0.0, 0.8);
        glVertex3f(0, 0, 0);
        glColor4f(1.0, 0.2, 0.0, 0.0);
        glVertex3f(0, 4.0 * i, 0);
    }
    glEnd();
    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawComet(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    glPushMatrix();
    
    /* Irregular Nucleus */
    glPushMatrix();
    glRotatef(pulse*5, 1, 0, 1);
    glColor3f(0.8, 0.8, 1.0);
    glScalef(1.2, 0.8, 0.9);
    glutSolidSphere(0.12, 10, 10);
    glPopMatrix();
    
    /* Coma (Glow) */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4, 0.6, 1.0, 0.5);
    glutSolidSphere(0.25, 16, 16);
    
    /* Volumetric Tail (Multiple cones/layers) */
    /* Point tail in negative X direction (opposite to movement) */
    glPushMatrix();
    glRotatef(90, 0, 1, 0); 
    for(int i=0; i<3; i++) {
        glPushMatrix();
        double length = 1.5 + i*0.5;
        double width = 0.2 + i*0.1;
        double alpha = 0.4 - i*0.1;
        glColor4f(0.5, 0.7, 1.0, alpha);
        glRotatef(sin(pulse*2 + i)*5, 1, 0, 0);
        glutSolidCone(width, length, 12, 4);
        glPopMatrix();
    }
    glPopMatrix();
    
    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawAsteroid(double x, double y, double z, double size) { (void)x; (void)y; (void)z;
    glUseProgram(hullShaderProgram);
    glPushMatrix();
    glColor3f(0.5, 0.35, 0.25); /* Brownish */
    glRotatef(pulse * 10.0, 1, 1, 0);
    
    double scale = 0.5 + size;
    glScalef(scale, scale, scale);

    /* Main rocky body (Irregular composition) */
    glPushMatrix();
    glScalef(1.2, 0.9, 1.1);
    glutSolidCube(0.2);
    glPopMatrix();
    
    /* Random bumps for a more 3D asteroid look */
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef(i*90, 0, 1, 1);
        glTranslatef(0.1, 0, 0);
        glScalef(0.6, 0.5, 0.7);
        glutSolidCube(0.15);
        glPopMatrix();
    }
    glPopMatrix();
    glUseProgram(0);
}

void drawDerelict(int ship_class, int faction) {
    glPushMatrix();
    /* Slow drifting rotation */
    glRotatef(pulse * 2.0, 0.3, 1.0, 0.2);
    /* Dark hull, no emissive lights */
    g_is_derelict_rendering = 1;
    glColor3f(0.3, 0.3, 0.32);
    
    if (faction == 0) { // FACTION_ALLIANCE
        drawAllianceShip(ship_class, 0, 0, 0);
    } else {
        /* Use Faction model for alien derelicts */
        switch (faction) {
            case 10: 
                drawKorthian(0, 0, 0); 
                break;
            case 11: 
                drawXylari(0, 0, 0); 
                break;
            case 12: 
                drawSwarm(0, 0, 0); 
                break;
            case 13: 
                drawVesperian(0, 0, 0); 
                break;
            case 14: 
                drawAscendant(0, 0, 0); 
                break;
            case 15: 
                drawQuarzite(0, 0, 0); 
                break;
            case 16: 
                drawSaurian(0, 0, 0); 
                break;
            case 17: 
                drawGilded(0, 0, 0); 
                break;
            case 18: 
                drawFluidicVoid(0, 0, 0); 
                break;
            case 19: 
                drawCryos(0, 0, 0); 
                break;
            case 20: 
                drawApex(0, 0, 0); 
                break;
            default: 
                drawAllianceShip(ship_class, 0, 0, 0); 
                break;
        }
    }
    g_is_derelict_rendering = 0;
    glPopMatrix();
}

void drawMine(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 50.0, 1, 0, 1);
    
    /* Core */
    glColor3f(0.4, 0.4, 0.45);
    glutSolidSphere(0.1, 8, 8);
    
    /* Spikes */
    glColor3f(0.3, 0.3, 0.3);
    for (int i = 0; i < 6; i++) {
        glPushMatrix();
        if (i == 0) {
            glRotatef(90, 1, 0, 0);
        } else if (i == 1) {
            glRotatef(-90, 1, 0, 0);
        } else if (i == 2) {
            glRotatef(90, 0, 1, 0);
        } else if (i == 3) {
            glRotatef(-90, 0, 1, 0);
        } else if (i == 4) {
            glRotatef(90, 0, 0, 1);
        } else if (i == 5) {
            glRotatef(-90, 0, 0, 1);
        }
        glTranslatef(0, 0, 0.15);
        glutSolidCone(0.02, 0.1, 4, 4);
        glPopMatrix();
    }
    
    /* Pulsing Light */
    double p = 0.5 + sin(pulse*10.0)*0.5;
    glDisable(GL_LIGHTING);
    glColor3f(p, 0, 0);
    glutSolidSphere(0.04, 8, 8);
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
}

void drawBuoy(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    /* Main body */
    glColor3f(0.6, 0.6, 0.7);
    glutSolidCube(0.15);
    
    /* Lattice structure */
    glColor3f(0.4, 0.4, 0.5);
    glBegin(GL_LINES);
    glVertex3f(0, 0, 0); glVertex3f(0, 0.5, 0);
    glVertex3f(0, 0.5, 0); glVertex3f(0.2, 0.7, 0);
    glVertex3f(0, 0.5, 0); glVertex3f(-0.2, 0.7, 0);
    glEnd();
    
    /* Rotating Antenna */
    glPushMatrix();
    glTranslatef(0, 0.5, 0);
    glRotatef(pulse * 40.0, 0, 1, 0);
    glColor3f(0.8, 0.8, 0.0);
    glBegin(GL_TRIANGLES);
    glVertex3f(0, 0, 0); glVertex3f(0.1, 0.2, 0.05); glVertex3f(0.1, 0.2, -0.05);
    glEnd();
    glPopMatrix();
    
    /* Blue Signal Rings (Pulse) */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double ring_s = fmod(pulse * 0.5, 1.0);
    glColor4f(0.0, 0.5, 1.0, 1.0 - ring_s);
    glPushMatrix();
    glRotatef(90, 1, 0, 0);
    glutWireTorus(0.01, ring_s * 0.8, 8, 20);
    glPopMatrix();
    glDisable(GL_BLEND);
    
    glPopMatrix();
}

void drawPlatform(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 5.0, 0, 1, 0);
    
    /* Main Hexagonal Body */
    glColor3f(0.4, 0.4, 0.4);
    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(i*120, 0, 1, 0);
        glScalef(1.0, 0.4, 0.3);
        glutSolidCube(1.0);
        glPopMatrix();
    }
    
    /* Ion Beam Banks (Top/Bottom) */
    glColor3f(0.2, 0.2, 0.2);
    glPushMatrix(); glTranslatef(0, 0.25, 0); glutSolidCylinder(0.1, 0.1, 12, 2); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.35, 0); glutSolidCylinder(0.1, 0.1, 12, 2); glPopMatrix();
    
    /* Energy Core */
    glDisable(GL_LIGHTING);
    glColor3f(1.0, 0.2, 0.0);
    glutSolidSphere(0.15, 12, 12);
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
}

void drawRift(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive blending */
    glPushMatrix();
    
    /* Rotating energy rings */
    for (int i = 0; i < 5; i++) {
        glPushMatrix();
        glRotatef(pulse * (20.0 + i * 10.0), 0.2, 1.0, 0.5);
        double r = 0.3 + i * 0.1;
        glColor4f(0.0, 0.8, 1.0, 0.6 - i * 0.1);
        glutWireTorus(0.02, r, 8, 24);
        glPopMatrix();
    }
    
    /* Inner crackle */
    glColor4f(1.0, 1.0, 1.0, 0.8);
    glutSolidSphere(0.1 + sin(pulse*20.0)*0.02, 8, 8);
    
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawMonster(int type, double x, double y, double z) { (void)x; (void)y; (void)z;
    if (type == 30) { /* Crystalline Entity */
        glDisable(GL_LIGHTING);
        glPushMatrix();
        glRotatef(pulse * 20.0, 1, 1, 1);
        glColor3f(1.0, 1.0, 1.0);
        glutWireIcosahedron();
        for (int i = 0; i < 4; i++) {
            glPushMatrix();
            glRotatef(i * 90, 0, 1, 0);
            glScalef(0.2, 2.0, 0.2);
            glColor4f(0.8, 0.5, 1.0, 0.8);
            glutSolidCube(1.0);
            glPopMatrix();
        }
        glPopMatrix();
        glEnable(GL_LIGHTING);
    } else if (type == 31) { /* Space Amoeba */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDisable(GL_LIGHTING);
        glPushMatrix();
        double s = 1.0 + sin(pulse * 2.0) * 0.1;
        glScalef(s, s * 0.8, s * 1.1);
        glColor4f(0.0, 0.6, 0.2, 0.6);
        glutSolidSphere(1.5, 16, 16);
        /* Nucleus */
        glColor4f(0.8, 0.2, 0.0, 0.8);
        glutSolidSphere(0.4, 8, 8);
        glPopMatrix();
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }
}

void drawDysonFragment(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 15.0, 0, 1, 0.5);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* Large fragmented shell */
    glColor4f(1.0, 0.8, 0.2, 0.4);
    glutWireSphere(1.0, 12, 12);
    
    /* Inner energy core */
    glColor4f(1.0, 0.4, 0.0, 0.8);
    glutSolidSphere(0.3 + sin(pulse*10.0)*0.05, 12, 12);
    
    /* Radial energy spikes */
    glBegin(GL_LINES);
    for(int i=0; i<8; i++) {
        glRotatef(45, 0, 1, 1);
        glVertex3f(0,0,0); glVertex3f(0, 1.2, 0);
    }
    glEnd();
    
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawTradingHub(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 5.0, 0, 1, 0);
    
    /* Central Spire */
    glColor3f(0.6, 0.6, 0.6);
    glPushMatrix(); glScalef(0.2, 2.0, 0.2); glutSolidCube(1.0); glPopMatrix();
    
    /* Rotating Rings */
    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(pulse * (10.0 + i*5.0), 0, 1, 0);
        glTranslatef(0, (i-1)*0.5, 0);
        glColor3f(0.4, 0.5, 0.8);
        glutWireTorus(0.05, 0.8 + i*0.2, 8, 32);
        
        /* Docking pads on rings */
        for(int j=0; j<4; j++) {
            glPushMatrix();
            glRotatef(j*90, 0, 1, 0);
            glTranslatef(0.8 + i*0.2, 0, 0);
            glColor3f(0.7, 0.7, 0.7);
            glutSolidCube(0.15);
            glPopMatrix();
        }
        glPopMatrix();
    }
    
    /* Beacon Light */
    glDisable(GL_LIGHTING);
    glColor3f(0, 1, 0);
    glPushMatrix(); glTranslatef(0, 1.0, 0); glutSolidSphere(0.05, 8, 8); glPopMatrix();
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
}

void drawAncientRelic(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 8.0, 1, 1, 1);
    glDisable(GL_LIGHTING);
    
    /* Strange geometric form */
    glColor3f(0.0, 1.0, 0.8);
    glutWireOctahedron();
    
    /* Inner glowing lattice */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.2, 0.8, 1.0, 0.5 + sin(pulse*5.0)*0.2);
    glutSolidIcosahedron();
    
    /* Floating orbiting shards */
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef(pulse * 30.0 + i*90, 0, 1, 0.2);
        glTranslatef(0.6, 0, 0);
        glColor4f(0.5, 1.0, 1.0, 0.8);
        glutSolidCube(0.08);
        glPopMatrix();
    }
    
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawSubspaceRupture(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glPushMatrix();
    
    /* Violent purple/red energy distortion */
    for(int i=0; i<6; i++) {
        glPushMatrix();
        glRotatef(pulse * (50.0 + i*15.0), 1.0, 0.2, 0.5);
        glScalef(1.0 + sin(pulse*10.0 + i)*0.2, 1.0, 1.0);
        glColor4f(0.6, 0.0, 0.8, 0.5 - i*0.05);
        glutWireTorus(0.05, 0.5 + i*0.1, 6, 16);
        glPopMatrix();
    }
    
    /* Core void */
    glColor4f(0.2, 0, 0.2, 0.9);
    glutSolidSphere(0.2, 12, 12);
    
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawSatellite(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 12.0, 0, 1, 0);
    
    /* Body */
    glColor3f(0.7, 0.7, 0.7);
    glutSolidCube(0.2);
    
    /* Solar Panels */
    glColor3f(0.1, 0.2, 0.5);
    glPushMatrix(); glTranslatef(0.3, 0, 0); glScalef(0.4, 0.02, 0.2); glutSolidCube(1.0); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3, 0, 0); glScalef(0.4, 0.02, 0.2); glutSolidCube(1.0); glPopMatrix();
    
    /* Antenna */
    glColor3f(0.5, 0.5, 0.5);
    glPushMatrix(); glTranslatef(0, 0.2, 0); glRotatef(-90, 1, 0, 0); glutSolidCone(0.05, 0.2, 8, 2); glPopMatrix();
    
    /* Blinking LED */
    if(fmod(pulse, 2.0) < 0.2) {
        glDisable(GL_LIGHTING);
        glColor3f(1, 0, 0);
        glPushMatrix(); glTranslatef(0, 0.1, 0.1); glutSolidSphere(0.02, 4, 4); glPopMatrix();
        glEnable(GL_LIGHTING);
    }
    
    glPopMatrix();
}

void drawIonStorm(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glPushMatrix();
    
    /* Large electrical clouds */
    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(pulse * (10.0 + i*5.0), 0.5, 1.0, 0.2);
        double s = 2.0 + i*0.5 + sin(pulse*2.0 + i)*0.2;
        glScalef(s, s*0.7, s*0.9);
        glColor4f(0.4, 0.4, 1.0, 0.2);
        glutWireSphere(1.0, 10, 10);
        glPopMatrix();
    }
    
    /* Electrical discharges */
    if(rand()%10 < 3) {
        glColor4f(1.0, 1.0, 1.0, 0.8);
        glBegin(GL_LINES);
        for(int i=0; i<5; i++) {
            glVertex3f(((rand()%200-100)/100.0)*2.0, ((rand()%200-100)/100.0)*2.0, ((rand()%200-100)/100.0)*2.0);
            glVertex3f(((rand()%200-100)/100.0)*2.0, ((rand()%200-100)/100.0)*2.0, ((rand()%200-100)/100.0)*2.0);
        }
        glEnd();
    }
    
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawAlienArtifact(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 20.0, 1, 1, 0);
    glColor3f(1.0, 0.5, 0.0);
    glutWireDodecahedron();
    glColor4f(1.0, 0.2, 0.0, 0.5);
    glutSolidDodecahedron();
    glPopMatrix();
}

void drawWarpGate(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 10.0, 0, 1, 0);
    glColor3f(0.0, 0.5, 1.0);
    glutWireTorus(0.05, 0.8, 8, 32);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.0, 0.8, 1.0, 0.3 + sin(pulse*4.0)*0.2);
    glutSolidTorus(0.04, 0.78, 8, 32);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawNeutronStar(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 100.0, 0, 1, 0); // Very fast spin
    glColor3f(0.8, 0.8, 1.0);
    glutSolidSphere(0.15, 24, 24);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.5, 0.5, 1.0, 0.6);
    glutWireSphere(0.2, 16, 16);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawMegaStructure(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 5.0, 1, 0, 1);
    glColor3f(0.5, 0.5, 0.5);
    glutWireCube(1.5);
    glColor3f(0.3, 0.3, 0.3);
    glutSolidCube(1.4);
    glPopMatrix();
}

void drawDarkCloud(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glPushMatrix();
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef(pulse * (5.0 + i*2.0), 0.5, 1.0, 0.2);
        glScalef(1.5 + i*0.2, 1.0, 1.2 + i*0.1);
        glColor4f(0.1, 0.0, 0.2, 0.4);
        glutSolidSphere(0.8, 12, 12);
        glPopMatrix();
    }
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawSingularity(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glColor3f(0.0, 0.0, 0.0);
    glutSolidSphere(0.3, 24, 24); // Pitch black core
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(pulse * (50.0 + i*20.0), 1, 1, 0);
        glColor4f(0.5, 0.0, 1.0, 0.3);
        glutWireTorus(0.02, 0.4 + i*0.1, 8, 32);
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawPlasmaStorm(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glPushMatrix();
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef(pulse * (15.0 + i*5.0), 1.0, 0.5, 0.0);
        glScalef(1.2 + sin(pulse*3.0 + i)*0.2, 1.0, 1.0);
        glColor4f(1.0, 0.2, 0.6, 0.3);
        glutWireSphere(0.8, 12, 12);
        glPopMatrix();
    }
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawOrbitalRing(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 15.0, 0, 1, 0);
    glColor3f(0.7, 0.7, 0.0);
    glutWireTorus(0.1, 1.0, 12, 48);
    glColor4f(0.4, 0.4, 0.0, 0.8);
    glutSolidTorus(0.08, 1.0, 12, 48);
    glPopMatrix();
}

void drawTimeAnomaly(double x, double y, double z) { (void)x; (void)y; (void)z;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glPushMatrix();
    glRotatef(pulse * -30.0, 0, 1, 0);
    glColor4f(0.0, 1.0, 0.5, 0.5);
    glutWireTeapot(0.5); // Abstract shape for anomaly
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawVoidCrystal(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    glRotatef(pulse * 10.0, 0, 1, 0);
    glColor3f(0.9, 0.0, 0.9);
    glutWireOctahedron();
    glColor4f(0.5, 0.0, 0.5, 0.7);
    glutSolidOctahedron();
    glPopMatrix();
}

void drawSubspaceAnomaly(double x, double y, double z) { (void)x; (void)y; (void)z;
    glPushMatrix();
    double s = 0.8 + sin(pulse * 20.0) * 0.2;
    glScalef(s, s, s);
    glRotatef(pulse * 45.0, 1, 1, 1);
    glColor3f(0.0, 1.0, 0.5); /* Seafoam Green */
    glutWireCube(0.5);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.0, 1.0, 0.8, 0.3);
    glutSolidCube(0.4);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawGalacticCompass() {
    glDisable(GL_LIGHTING);
    glPushMatrix();
    /* Position the compass at the top center, aligned with the vertical axis */
    glTranslatef(0.0, 9.0, 0.0);
    
    double len = 1.5;
    glLineWidth(1.0);
    
    /* Z-Axis (0/180 Degrees) - Blue */
    glColor3f(0.3, 0.3, 1.0);
    glBegin(GL_LINES);
    glVertex3f(0, 0, -len);
    glVertex3f(0, 0, len);
    glEnd();
    drawText3D(0, 0, len + 0.2, "0");
    drawText3D(0, 0, -len - 0.5, "180");

    /* X-Axis (90/270 Degrees) - Red */
    glColor3f(1.0, 0.3, 0.3);
    glBegin(GL_LINES);
    glVertex3f(-len, 0, 0);
    glVertex3f(len, 0, 0);
    glEnd();
    drawText3D(len + 0.2, 0, 0, "90");
    drawText3D(-len - 0.5, 0, 0, "270");

    /* Y-Axis (Mark) - Green */
    glColor3f(0.3, 1.0, 0.3);
    glBegin(GL_LINES);
    glVertex3f(0, -len, 0);
    glVertex3f(0, len, 0);
    glEnd();
    drawText3D(0, len + 0.2, 0, "M:+90");
    drawText3D(0, -len - 0.5, 0, "M:-90");
    
    glColor3f(1, 1, 1);
    glutWireSphere(0.1, 8, 8);
    
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawExplorerMap() {
    drawGalacticCompass();
    glDisable(GL_LIGHTING);
    double gap = 1.2;
    double offset = - ((double)GALAXY_SIZE * gap) / 2.0;

    /* Draw full grid frame */
    glColor4f(0.2, 0.2, 0.5, 0.3);
    glPushMatrix();
    glTranslatef(0, 0, 0);
    glScalef((double)GALAXY_SIZE * gap, (double)GALAXY_SIZE * gap, (double)GALAXY_SIZE * gap);
    glutWireCube(1.0);
    glPopMatrix();

    /* Draw Vertex Coordinates for orientation */
    glColor3f(0.5, 0.5, 0.5);
    char vbuf[32];
    int v_coords[] = {1, GALAXY_SIZE};
    for(int vz=0; vz<2; vz++) {
        for(int vy=0; vy<2; vy++) {
            for(int vx=0; vx<2; vx++) {
                int cx = v_coords[vx], cy = v_coords[vy], cz = v_coords[vz];
                double px = offset + (cx - 0.5) * gap;
                double py = offset + (cz - 0.5) * gap;
                double pz = offset + ((double)GALAXY_SIZE + 0.5 - cy) * gap; /* Inverted Y for map display consistency */
                sprintf(vbuf, "[%d,%d,%d]", cx, cy, cz);
                drawText3D(px, py + 0.3, pz, vbuf);
            }
        }
    }

    for(int z=1; z<=GALAXY_SIZE; z++) {
        for(int y=1; y<=GALAXY_SIZE; y++) {
            for(int x=1; x<=GALAXY_SIZE; x++) {
                int64_t val = g_galaxy[x][y][z];
                bool is_my_q = (x == g_my_q[0] && y == g_my_q[1] && z == g_my_q[2]);
                if (val == 0 && !is_my_q) continue;

                double px = offset + (x - 0.5) * gap;
                double py = offset + (z - 0.5) * gap;
                double pz = offset + ((double)GALAXY_SIZE + 0.5 - y) * gap;

                /* Use absolute value for digit extraction to handle supernova (negative) quadrants */
                int64_t uval = (val < 0) ? -val : val;

                /* Color coding based on Q|M|U|R|T|B|M|D|A|C|S|Pu|N|BH|P|K|B|S (18 digits) */
                int quasar   = (uval / 100000000000000000LL) % 10;
                int monster  = (uval / 10000000000000000LL) % 10;
                int player   = (uval / 1000000000000000LL) % 10;
                int rift     = (uval / 100000000000000LL) % 10;
                int platform = (uval / 10000000000000LL) % 10;
                int buoy     = (uval / 1000000000000LL) % 10;
                int mine     = (uval / 100000000000LL) % 10;
                int derelict = (uval / 10000000000LL) % 10;
                int asteroid = (uval / 1000000000LL) % 10;
                int comet = (uval / 100000000LL) % 10;
                int storm = (uval / 10000000LL) % 10;
                int pul   = (uval / 1000000LL) % 10;
                int neb   = (uval / 100000LL) % 10;
                int bh    = (uval / 10000LL) % 10;
                int pl    = (uval / 1000LL) % 10;
                int en    = (uval / 100LL) % 10;
                int bs    = (uval / 10LL) % 10;
                int st    = (uval) % 10;

                /* Apply Filter */
                if (g_map_filter > 0) {
                    bool match = false;
                    switch(g_map_filter) {
                        case 1: if(st>0) match=true; break;
                        case 2: if(pl>0) match=true; break;
                        case 3: if(bs>0) match=true; break;
                        case 4: if(en>0 || player>0) match=true; break;
                        case 5: if(bh>0) match=true; break;
                        case 6: if(neb>0) match=true; break;
                        case 7: if(pul>0) match=true; break;
                        case 8: if(storm>0) match=true; break;
                        case 9: if(comet>0) match=true; break;
                        case 10: if(asteroid>0) match=true; break;
                        case 11: if(derelict>0) match=true; break;
                        case 12: if(mine>0) match=true; break;
                        case 13: if(buoy>0) match=true; break;
                        case 14: if(platform>0) match=true; break;
                        case 15: if(rift>0) match=true; break;
                        case 16: if(monster>0) match=true; break;
                        case 17: if(quasar>0) match=true; break;
                    }
                    if (!match && !is_my_q) continue;
                }

                glPushMatrix();
                glTranslatef(px, py, pz);
                
                if (is_my_q) {
                    /* Highlight current quadrant: Pulsing White + Label */
                    double s_glow = 0.4 + sin(pulse*6.0)*0.15;
                    glColor4f(1, 1, 1, 0.8);
                    glutWireCube(s_glow);
                    
                    /* Directional Vector (Heading Needle) - Tensor Style */
                    glDisable(GL_LIGHTING);
                    double r_h = g_shared_state->shm_h * M_PI / 180.0;
                    double r_m = g_shared_state->shm_m * M_PI / 180.0;
                    
                    /* Map axes to screen coordinates */
                    double v_x = sin(r_h) * cos(r_m);
                    double v_y = sin(r_m);
                    double v_z = cos(r_h) * cos(r_m);
                    
                    double line_len = gap * 0.75;
                    glLineWidth(1.0);
                    glBegin(GL_LINES);
                    glColor3f(1.0, 1.0, 0.0); /* Bright Yellow Line */
                    glVertex3f(0, 0, 0);
                    glVertex3f(v_x * line_len, v_y * line_len, v_z * line_len);
                    glEnd();
                    
                    /* Pointer head - Red Cone Tip */
                    glPushMatrix();
                    glTranslatef(v_x * line_len, v_y * line_len, v_z * line_len);
                    /* Rotate cone to point in the same direction as the vector */
                    glRotatef(g_shared_state->shm_h, 0, 1, 0);
                    glRotatef(-g_shared_state->shm_m, 1, 0, 0);
                    glColor3f(1.0, 0.0, 0.0); /* Red Pointer */
                    glutSolidCone(0.1, 0.3, 8, 8);
                    glPopMatrix();
                    
                    glLineWidth(1.0);
                    
                    glColor3f(1, 1, 1);
                    char you_msg[64];
                    sprintf(you_msg, "YOU (H:%03.0f M:%+03.0f)", g_shared_state->shm_h, g_shared_state->shm_m);
                    drawText3D(-0.3, 0.4, 0, you_msg);
                    glEnable(GL_LIGHTING);
                }

                if (g_sn_pos.active && x == g_sn_q[0] && y == g_sn_q[1] && z == g_sn_q[2]) {
                    /* Supernova Warning - Red Pulsing (Global Broadcast) */
                    double blink = (sin(pulse * 10.0) + 1.0) * 0.5;
                    glColor4f(1.0, 0.0, 0.0, 0.3 + blink * 0.5);
                    glutSolidCube(gap * 0.8);
                }

                if (storm > 0) {
                    /* Ion Storm: Large Transparent White Wireframe Shell - Enhanced visibility */
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    glLineWidth(1.0);
                    glColor4f(1.0, 1.0, 1.0, 0.4 + sin(pulse*4.0)*0.2);
                    glutWireCube(0.8);
                    glLineWidth(1.0);
                    glDisable(GL_BLEND);
                }

                if (val != 0) {
                    /* If a filter is active, force the color of the filtered object */
                    int effective_filter = g_map_filter;
                    
                    /* If no filter, we use the default priority chain, 
                       otherwise we jump straight to the filtered color */
                    if (effective_filter == 17 || (effective_filter == 0 && quasar > 0)) glColor3f(1.0, 0.0, 1.0); /* Magenta - Quasar */
                    else if (effective_filter == 16 || (effective_filter == 0 && monster > 0)) glColor3f(1.0, 1.0, 1.0); /* White - Monster */
                    else if (effective_filter == 15 || (effective_filter == 0 && rift > 0)) glColor3f(0.0, 1.0, 1.0); /* Cyan - Rift */
                    else if (effective_filter == 14 || (effective_filter == 0 && platform > 0)) glColor3f(0.8, 0.4, 0.0); /* Dark Orange - Platform */
                    else if (effective_filter == 13 || (effective_filter == 0 && buoy > 0)) glColor3f(0.0, 0.5, 1.0); /* Blue - Buoy */
                    else if (effective_filter == 12 || (effective_filter == 0 && mine > 0)) glColor3f(1.0, 0.0, 0.0); /* Bright Red - Mine */
                    else if (effective_filter == 11 || (effective_filter == 0 && derelict > 0)) glColor3f(0.3, 0.3, 0.3); /* Dark Grey - Derelict */
                    else if (effective_filter == 10 || (effective_filter == 0 && asteroid > 0)) glColor3f(0.5, 0.3, 0.1); /* Brown - Asteroid */
                    else if (effective_filter == 9 || (effective_filter == 0 && comet > 0)) glColor3f(0.5, 0.8, 1.0); /* Light Blue - Comet */
                    else if (effective_filter == 8 || (effective_filter == 0 && storm > 0)) glColor3f(1.0, 1.0, 1.0); /* White - Ion Storm */
                    else if (effective_filter == 7 || (effective_filter == 0 && pul > 0)) glColor3f(1.0, 0.5, 0); /* Orange - Pulsar */
                    else if (effective_filter == 6 || (effective_filter == 0 && neb > 0)) glColor3f(0.7, 0.7, 0.7); /* Grey - Nebula */
                    else if (effective_filter == 5 || (effective_filter == 0 && bh > 0)) glColor3f(0.6, 0, 1.0); /* Purple - Black Hole */
                    else if (effective_filter == 4 || (effective_filter == 0 && (en > 0 || player > 0))) glColor3f(1, 0, 0); /* Red - Hostile/Vessel */
                    else if (effective_filter == 3 || (effective_filter == 0 && bs > 0)) glColor3f(0, 1, 0); /* Green - Base */
                    else if (effective_filter == 2 || (effective_filter == 0 && pl > 0)) glColor3f(0, 0.8, 1); /* Cyan - Planet */
                    else if (effective_filter == 1 || (effective_filter == 0 && st > 0)) glColor3f(1, 1, 0); /* Yellow - Star */
                    else glColor3f(0.4, 0.4, 0.4); /* Dark gray fallback */
                    
                    double base_s = 0.15;
                    if (quasar > 0) base_s = 0.2 + sin(pulse*15.0)*0.08; /* Fast violent pulse */
                    if (pul > 0) base_s += sin(pulse*8.0)*0.05; /* Pulsar heartbeat */
                    if (monster > 0) base_s = 0.25 + sin(pulse*5.0)*0.05;
                    if (mine > 0) base_s = 0.1;
                    if (buoy > 0) base_s = 0.12;
                    if (platform > 0) base_s = 0.18;
                    if (rift > 0) base_s = 0.2;
                    glutSolidCube(base_s);
                }
                glPopMatrix();
            }
        }
    }
    glEnable(GL_LIGHTING);
}

void drawGrid() {
    glDisable(GL_LIGHTING); glColor4f(0.5, 0.5, 0.5, 0.2);
    if (vbo_grid != 0) { glEnableClientState(GL_VERTEX_ARRAY); glBindBuffer(GL_ARRAY_BUFFER, vbo_grid); glVertexPointer(3, GL_FLOAT, 0, 0); glDrawArrays(GL_LINES, 0, grid_vertex_count); glBindBuffer(GL_ARRAY_BUFFER, 0); glDisableClientState(GL_VERTEX_ARRAY); }
    glEnable(GL_LIGHTING);
}

void drawShipTrail(int obj_idx) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glLineWidth(1.0);
    glBegin(GL_LINE_STRIP);
    
    double r=0.4, g=0.7, b=1.0; /* Default Alliance Command Blue */
    if (objects[obj_idx].type == 10) { r=1.0; g=0.1; b=0.0; } /* Korthian Red */
    if (objects[obj_idx].type == 11) { r=0.0; g=1.0; b=0.2; } /* Xylari Green */
    if (objects[obj_idx].type == 12) { r=0.0; g=0.8; b=0.8; } /* Swarm Cyan */

    for (int i = 0; i < objects[obj_idx].trail_count; i++) {
        int idx = (objects[obj_idx].trail_ptr - 1 - i + MAX_TRAIL) % MAX_TRAIL;
        double alpha = (1.0 - i / objects[obj_idx].trail_count) * 0.5;
        glColor4f(r, g, b, alpha);
        glVertex3f(objects[obj_idx].trail[idx][0], objects[obj_idx].trail[idx][1], objects[obj_idx].trail[idx][2]);
        
        /* Spawn a subtle engine particle occasionally at the latest trail point */
        if (i == 0 && (rand() % 5 == 0)) {
            spawnParticle(objects[obj_idx].x, objects[obj_idx].y, objects[obj_idx].z, 0, 0, 0, r, g, b, 0.5, 0.3);
        }
    }
    glEnd();
    glPopAttrib();
}

void drawIonBeams() {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int i = 0; i < 64; i++) {
        if (beams[i].alpha > 0) {
            glLineWidth(3.0); glColor4f(1.0, 0.8, 0.0, (beams[i].alpha > 1.0) ? 1.0 : beams[i].alpha);
            glBegin(GL_LINES); 
            glVertex3f(beams[i].sx, beams[i].sy, beams[i].sz); 
            glVertex3f(beams[i].tx, beams[i].ty, beams[i].tz); 
            glEnd();
        }
    }
    glPopAttrib();
}

void drawTorpedoExplosion(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0, 0.4, 0.1, 0.6);
    glutSolidSphere(1.2, 24, 24);
    glColor4f(1.0, 1.0, 0.8, 0.9);
    glutSolidSphere(0.6, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawExplosion() {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glUseProgram(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    for (int b = 0; b < 10; b++) {
        if (g_booms[b].timer <= 0) continue;

        /* Spawn particles on first frame of this explosion */
        if (g_booms[b].timer == (int)(0.66 * GAME_TICK_RATE)) {
            for (int i = 0; i < 100; i++) {
                double vx = ((double)rand() / (double)RAND_MAX - 0.5) * 0.3;
                double vy = ((double)rand() / (double)RAND_MAX - 0.5) * 0.3;
                double vz = ((double)rand() / (double)RAND_MAX - 0.5) * 0.3;
                spawnParticle(g_booms[b].x, g_booms[b].y, g_booms[b].z, vx, vy, vz, 1.0, 0.7, 0.2, 1.2, 1.0);
            }
        }

        glPushMatrix(); 
        glTranslatef(g_booms[b].x, g_booms[b].y, g_booms[b].z);
        
        /* Pulsing expansion limited to max radius 1.0 (2 sectors total volume) */
        double s = ((int)(0.66 * GAME_TICK_RATE) - g_booms[b].timer) * 0.025; 
        if (s > 1.0) s = 1.0;
        
        double alpha = g_booms[b].timer / (0.66 * GAME_TICK_RATE);
        glColor4f(1.0, 0.6, 0.1, alpha); 
        glutSolidSphere(s, 24, 24);
        
        /* Secondary inner glow */
        glColor4f(1.0, 1.0, 0.8, alpha * 0.5);
        glutSolidSphere(s * 0.6, 16, 16);
        
        glPopMatrix(); 
    }
    
    glDisable(GL_BLEND);
    glPopAttrib();
}

void drawJumpArrival_v1() {
    if (g_jump_arrival.timer <= 0) return;
    
    glPushMatrix();
    glTranslatef(g_jump_arrival.x, g_jump_arrival.y, g_jump_arrival.z);
    
    /* Phase 1: Brilliant White Wormhole - Starts closing at 180 */
    double wh_scale = (g_jump_arrival.timer < 180) ? (g_jump_arrival.timer / 180.0) : 1.0;
    
    glPushMatrix();
    glScalef(wh_scale, wh_scale, wh_scale);
    drawWormhole(0, 0, 0, g_jump_arrival.h, g_jump_arrival.m, 1, 1); /* Type 1: Arrival (White), Ver 1: Legacy */
    glPopMatrix();
    
    /* Phase 3: Final Flash - Starts at 180 */
    if (g_jump_arrival.timer < 180) {
        double flash_t = 1.0 - (g_jump_arrival.timer / 180.0);
        double alpha = (1.0 - flash_t) * 0.9;
        double base_r = flash_t * 0.5; /* Base for 2 sectors diameter */
        
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        /* 1. Chromatic Volumetric Glow */
        struct { double r, g, b, a, scale; } layers[] = {
            {1.0, 1.0, 1.0, 1.0, 0.4},  /* Core: Pure White */
            {0.0, 1.0, 1.0, 0.8, 0.8},  /* Inner: Bright Cyan */
            {0.0, 0.4, 1.0, 0.6, 1.2},  /* Middle: Electric Blue */
            {0.3, 0.0, 0.8, 0.4, 1.6},  /* Outer: Deep Purple */
            {0.1, 0.0, 0.4, 0.2, 2.0}   /* Aura: Dark Violet */
        };
        for(int i=0; i<5; i++) {
            glColor4f(layers[i].r, layers[i].g, layers[i].b, alpha * layers[i].a);
            glutSolidSphere(base_r * layers[i].scale, 16, 16);
        }

        /* 2. Converging Arrival Particles */
        glPointSize(3.0);
        glBegin(GL_POINTS);
        for(int i=0; i<150; i++) {
            if (g_arrival_fx.particles[i].active) {
                double p_alpha = alpha * (g_jump_arrival.timer / 180.0);
                glColor4f(g_arrival_fx.particles[i].r, g_arrival_fx.particles[i].g, g_arrival_fx.particles[i].b, p_alpha);
                glVertex3f(g_arrival_fx.particles[i].x - g_jump_arrival.x, 
                           g_arrival_fx.particles[i].y - g_jump_arrival.y, 
                           g_arrival_fx.particles[i].z - g_jump_arrival.z);
            }
        }
        glEnd();
        
        /* 3. 3D Volumetric details */
        for(int i=0; i<2; i++) {
            if (i == 0) glColor4f(0.0, 1.0, 1.0, alpha * 0.5); 
            else glColor4f(0.8, 0.0, 1.0, alpha * 0.4);
            
            glPushMatrix();
            glRotatef(pulse * (40.0 + i*20.0), 0, 1, 0);
            glRotatef(pulse * (30.0 - i*15.0), 1, 0, 0);
            glutWireSphere(base_r * (1.2 + i*0.4), 12, 12);
            glPopMatrix();
        }
        
        glLineWidth(2.5);
        for(int axis=0; axis<3; axis++) {
            glPushMatrix();
            if(axis==1) glRotatef(90, 0, 1, 0);
            if(axis==2) glRotatef(90, 1, 0, 0);
            glRotatef(pulse * 150.0, 0, 0, 1);
            glBegin(GL_LINES);
            double len = base_r * 2.0;
            for(int j=0; j<4; j++) {
                double ang = j * (M_PI / 2.0);
                glColor4f(1.0, 1.0, 1.0, alpha); glVertex3f(0, 0, 0);
                glColor4f(0.0, 0.2, 1.0, 0.0); glVertex3f(cos(ang)*len, sin(ang)*len, 0);
            }
            glEnd();
            glPopMatrix();
        }
        glPopAttrib();
    }

    glPopMatrix();
}

void drawJumpArrival_v2() {
    if (g_jump_arrival.timer <= 0) return;
    
    glPushMatrix();
    glTranslatef(g_jump_arrival.x, g_jump_arrival.y, g_jump_arrival.z);
    
    /* Phase 1: Brilliant White Wormhole - Starts closing at 180 */
    double wh_scale = (g_jump_arrival.timer < 180) ? (g_jump_arrival.timer / 180.0) : 1.0;
    
    glPushMatrix();
    glScalef(wh_scale, wh_scale, wh_scale);
    drawWormhole(0, 0, 0, g_jump_arrival.h, g_jump_arrival.m, 1, 2); /* Type 1: Arrival (White), Ver 2: Current */
    glPopMatrix();
    
    /* Phase 3: Final Flash - Starts at 180 */
    if (g_jump_arrival.timer < 180) {
        double flash_t = 1.0 - (g_jump_arrival.timer / 180.0);
        double alpha = (1.0 - flash_t) * 0.9;
        double base_r = flash_t * 0.5; /* Base for 2 sectors diameter */
        
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        /* 1. Chromatic Volumetric Glow */
        struct { double r, g, b, a, scale; } layers[] = {
            {1.0, 1.0, 1.0, 1.0, 0.4},  /* Core: Pure White */
            {0.0, 1.0, 1.0, 0.8, 0.8},  /* Inner: Bright Cyan */
            {0.0, 0.4, 1.0, 0.6, 1.2},  /* Middle: Electric Blue */
            {0.3, 0.0, 0.8, 0.4, 1.6},  /* Outer: Deep Purple */
            {0.1, 0.0, 0.4, 0.2, 2.0}   /* Aura: Dark Violet */
        };
        for(int i=0; i<5; i++) {
            glColor4f(layers[i].r, layers[i].g, layers[i].b, alpha * layers[i].a);
            glutSolidSphere(base_r * layers[i].scale, 16, 16);
        }

        /* 2. Converging Arrival Particles */
        glPointSize(3.0);
        glBegin(GL_POINTS);
        for(int i=0; i<150; i++) {
            if (g_arrival_fx.particles[i].active) {
                double p_alpha = alpha * (g_jump_arrival.timer / 180.0);
                glColor4f(g_arrival_fx.particles[i].r, g_arrival_fx.particles[i].g, g_arrival_fx.particles[i].b, p_alpha);
                glVertex3f(g_arrival_fx.particles[i].x - g_jump_arrival.x, 
                           g_arrival_fx.particles[i].y - g_jump_arrival.y, 
                           g_arrival_fx.particles[i].z - g_jump_arrival.z);
            }
        }
        glEnd();
        
        /* 3. 3D Volumetric details */
        for(int i=0; i<2; i++) {
            if (i == 0) glColor4f(0.0, 1.0, 1.0, alpha * 0.5); 
            else glColor4f(0.8, 0.0, 1.0, alpha * 0.4);
            
            glPushMatrix();
            glRotatef(pulse * (40.0 + i*20.0), 0, 1, 0);
            glRotatef(pulse * (30.0 - i*15.0), 1, 0, 0);
            glutWireSphere(base_r * (1.2 + i*0.4), 12, 12);
            glPopMatrix();
        }
        
        glLineWidth(2.5);
        for(int axis=0; axis<3; axis++) {
            glPushMatrix();
            if(axis==1) glRotatef(90, 0, 1, 0);
            if(axis==2) glRotatef(90, 1, 0, 0);
            glRotatef(pulse * 150.0, 0, 0, 1);
            glBegin(GL_LINES);
            double len = base_r * 2.0;
            for(int j=0; j<4; j++) {
                double ang = j * (M_PI / 2.0);
                glColor4f(1.0, 1.0, 1.0, alpha); glVertex3f(0, 0, 0);
                glColor4f(0.0, 0.2, 1.0, 0.0); glVertex3f(cos(ang)*len, sin(ang)*len, 0);
            }
            glEnd();
            glPopMatrix();
        }
        glPopAttrib();
    }

    glPopMatrix();
}

void drawJumpArrival() {
    if (g_jump_arrival.jump_type == 1) drawJumpArrival_v1();
    else drawJumpArrival_v2();
}

/* Personal Probe HUD Data Sync Only (Rendering is now universal via drawObject Type 27) */
void updateProbeHUD() {
    /* No 3D rendering here anymore */
}

void drawQuasar(double x, double y, double z, int type) {
    glDisable(GL_LIGHTING);
    glPushMatrix(); 
    
    /* Position and basic alignment */
    /* Quasars are celestial monsters, they dominate the view */
    
    /* Seed for procedural variation based on position */
    int seed = (int)(x + y + z);
    
    /* CONFIGURATION BASED ON TYPE */
    float r=1, g=1, b=1;     /* Core Color */
    float jr=1, jg=1, jb=1;  /* Jet Color */
    float core_size = 1.0;
    float disk_size = 3.0;
    float jet_len = 15.0;
    float jet_width = 0.5;
    float pulse_speed = 1.0;
    bool has_jets = true;
    bool has_dark_ring = false;
    bool is_violent = false;
    bool has_wind = false;

    switch(type) {
        case 0: /* Radio-loud: Classic Blue, Massive Jets */
            r=0.2; g=0.6; b=1.0; 
            jr=0.1; jg=0.3; jb=1.0;
            core_size=0.8; disk_size=2.5; jet_len=25.0; jet_width=0.6;
            break;
        case 1: /* Radio-quiet: Gold/White, Huge Disk, Tiny Jets */
            r=1.0; g=0.9; b=0.6; 
            jr=1.0; jg=0.8; jb=0.4;
            core_size=1.2; disk_size=4.5; jet_len=3.0; jet_width=0.2;
            break;
        case 2: /* BAL (Broad Absorption): Emerald/Acid, Turbulent Winds */
            r=0.1; g=1.0; b=0.4; 
            jr=0.0; jg=0.8; jb=0.2;
            core_size=0.9; disk_size=3.0; jet_len=10.0; jet_width=0.8;
            has_wind = true;
            break;
        case 3: /* Type 2: Obscured, Orange/Rust, Dark Dust Torus */
            r=1.0; g=0.5; b=0.1; 
            jr=1.0; jg=0.3; jb=0.0;
            core_size=0.6; disk_size=2.0; jet_len=12.0; jet_width=0.3;
            has_dark_ring = true;
            break;
        case 4: /* Red Quasar: Crimson, Heavy, Slow Pulse */
            r=1.0; g=0.1; b=0.1; 
            jr=0.8; jg=0.0; jb=0.0;
            core_size=1.5; disk_size=2.8; jet_len=8.0; jet_width=1.5;
            pulse_speed = 0.3;
            break;
        case 5: /* OVV (Optically Violent): Violet/White, Stroboscopic */
            r=0.8; g=0.6; b=1.0; 
            jr=0.9; jg=0.8; jb=1.0;
            core_size=0.8; disk_size=1.5; jet_len=30.0; jet_width=0.2;
            is_violent = true;
            pulse_speed = 5.0;
            break;
        case 6: /* Weak emission: Pale Cyan, Ghostly, Thin */
            r=0.6; g=0.8; b=0.9; 
            jr=0.5; jg=0.7; jb=0.8;
            core_size=0.5; disk_size=2.0; jet_len=18.0; jet_width=0.1;
            break;
        default: /* Fallback */
            r=0.5; g=0.5; b=1.0;
            break;
    }

    /* Calculation of dynamic pulsation */
    double t = pulse * pulse_speed;
    double p_factor = 1.0 + sin(t) * (is_violent ? 0.3 : 0.1);
    if (is_violent) p_factor += (rand()%100)/500.0; /* Jitter for OVV */

    /* Global Rotation */
    glRotatef(pulse * 2.0 + seed, 0, 1, 0); /* Orbit rotation */
    /* Tilt the quasar to show the disk */
    glRotatef(30.0 + sin(pulse*0.5)*10.0, 1, 0, 1); 

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    /* --- 1. THE RELATIVISTIC JETS --- */
    if (has_jets) {
        for(int dir=-1; dir<=1; dir+=2) {
            glPushMatrix();
            glRotatef(dir == 1 ? 0 : 180, 1, 0, 0);
            
            /* Inner Beam (Solid energy) */
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(jr, jg, jb, 0.8 * p_factor);
            glVertex3f(0, 0, 0); 
            glColor4f(jr, jg, jb, 0.0);
            for(int i=0; i<=12; i++) {
                float ang = i * M_PI * 2.0 / 12.0;
                glVertex3f(cos(ang)*jet_width*0.3, jet_len * p_factor, sin(ang)*jet_width*0.3);
            }
            glEnd();

            /* Outer Sheath (Diffuse glow) */
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(jr*0.5, jg*0.5, jb*0.5, 0.3);
            glVertex3f(0, 0, 0);
            glColor4f(0, 0, 0, 0.0);
            for(int i=0; i<=12; i++) {
                float ang = i * M_PI * 2.0 / 12.0;
                glVertex3f(cos(ang)*jet_width, jet_len * 0.7, sin(ang)*jet_width);
            }
            glEnd();
            
            /* Shockwaves (Rings traveling up the jet) - Only for high energy ones */
            if (jet_len > 10.0) {
                double shock_off = fmod(pulse * 2.0, jet_len * 0.8);
                glPushMatrix();
                glTranslatef(0, shock_off, 0);
                glRotatef(90, 1, 0, 0);
                glColor4f(jr, jg, jb, 0.4 * (1.0 - shock_off/jet_len));
                glutWireTorus(jet_width * 0.5, jet_width * 1.5, 4, 12);
                glPopMatrix();
            }

            glPopMatrix();
        }
    }

    /* --- 2. ACCRETION DISK --- */
    glPushMatrix();
    /* Rotate disk texture */
    glRotatef(pulse * (is_violent ? 80.0 : 30.0), 0, 1, 0);
    glScalef(1.0, 0.05, 1.0); /* Flatten */
    
    /* Main glowing disk */
    glColor4f(r, g, b, 0.5);
    glutSolidTorus(disk_size * 0.2, disk_size, 16, 32);
    
    /* Inner hot ring */
    glColor4f(1.0, 1.0, 1.0, 0.8);
    glutSolidTorus(disk_size * 0.1, disk_size * 0.4, 8, 24);
    
    glPopMatrix();

    /* --- 3. SPECIAL FEATURES --- */
    if (has_dark_ring) {
        /* Type 2 Obscuring Torus */
        glPushMatrix();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Solid/Dark blending */
        glScalef(1.0, 0.2, 1.0);
        glColor4f(0.1, 0.05, 0.0, 0.9); /* Dark Dust */
        glutSolidTorus(disk_size * 0.3, disk_size * 1.5, 10, 32);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Back to additive */
        glPopMatrix();
    }

    if (has_wind) {
        /* BAL Quasar Wind - Particles orbiting */
        for(int w=0; w<3; w++) {
            glPushMatrix();
            glRotatef(pulse * (40.0 + w*20.0), 0, 1, 0);
            glRotatef(w * 45.0, 1, 0, 0);
            glColor4f(0.0, 1.0, 0.5, 0.3);
            glBegin(GL_LINE_LOOP);
            for(int k=0; k<16; k++) {
                double a = k * M_PI * 2.0 / 16.0;
                glVertex3f(cos(a)*disk_size*1.2, sin(a)*disk_size*0.2, 0);
            }
            glEnd();
            glPopMatrix();
        }
    }

    /* --- 4. THE CORE --- */
    /* Blindingly bright center */
    glColor4f(1.0, 1.0, 1.0, 1.0);
    glutSolidSphere(core_size * p_factor, 16, 16);
    
    /* Halo */
    glColor4f(r, g, b, 0.4);
    glutSolidSphere(core_size * 2.0 * p_factor, 16, 16);

    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawTorpedoModel(double x, double y, double z) {
    glPushMatrix(); 
    glTranslatef(x, y, z);
    
    /* 3. Solid Core (Robust Sphere for visibility) */
    glEnable(GL_LIGHTING);
    glColor3f(1.0, 1.0, 0.0); /* Bright Yellow */
    glutSolidSphere(0.2, 16, 16); 
    glDisable(GL_LIGHTING);

    glPopMatrix();
}

void drawTorpedo() {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    for (int s = 0; s < MAX_VISIBLE_TORPEDOES; s++) {
        if (g_torps[s].active) {
            glDisable(GL_DEPTH_TEST);
            
            glPushMatrix();
            float dx = (g_torps[s].q1 - g_shared_state->shm_q[0]) * QUADRANT_SIZE;
            float dy = (g_torps[s].q2 - g_shared_state->shm_q[1]) * QUADRANT_SIZE;
            float dz = (g_torps[s].q3 - g_shared_state->shm_q[2]) * QUADRANT_SIZE;
            glTranslatef(g_torps[s].x + dx, g_torps[s].y + dy, g_torps[s].z + dz);
            
            /* Enhanced Red Glow/Core Logic */
            double wave = (sin(pulse * 30.0) + 1.0) * 0.5;
            
            /* 1. Enhanced Red Halo */
            drawGlow(0.12 + wave * 0.05, 1.0, 0.2, 0.1, 0.7);

            /* 2. Luminous Red/Yellow Star Rays */
            glLineWidth(2.0);
            for(int axis=0; axis<3; axis++) {
                glPushMatrix();
                if(axis==1) glRotatef(90, 0, 1, 0);
                if(axis==2) glRotatef(90, 1, 0, 0);
                glRotatef(pulse * 250.0, 0, 0, 1);
                glBegin(GL_LINES);
                for(int j=0; j<4; j++) {
                    double ang = j * (M_PI / 2.0);
                    double len = 0.8 + wave * 0.3;
                    glColor4f(1.0, 0.3, 0.1, 1.0); glVertex3f(0, 0, 0);
                    glColor4f(1.0, 0.8, 0.0, 0.0); glVertex3f(cos(ang)*len, sin(ang)*len, 0);
                }
                glEnd();
                glPopMatrix();
            }

            /* 3. Solid Icosahedron Core */
            glEnable(GL_LIGHTING);
            glColor3f(1.0, 0.5, 0.0); /* Orange-Red glow core */
            glPushMatrix();
            glScalef(0.15, 0.15, 0.15);
            glRotatef(pulse * 400.0, 1, 1, 1);
            glutSolidIcosahedron(); 
            glPopMatrix();
            glDisable(GL_LIGHTING);

            glPopMatrix();
        }
    }
    glPopAttrib();
}
void drawRecoveryEffect() {
    if (g_recovery_fx.timer <= 0) return;
    
    double x = g_recovery_fx.x;
    double y = g_recovery_fx.y;
    double z = g_recovery_fx.z;
    double t = g_recovery_fx.timer / 60.0; 
    
    glDisable(GL_LIGHTING);
    glUseProgram(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* 1. Golden Transporter Beam (Vertical) - Radius reduced to 0.12 */
    double beam_alpha = (t > 0.5) ? (1.0 - t) * 2.0 : (t * 2.0);
    glColor4f(1.0, 0.9, 0.3, beam_alpha * 0.7); 
    
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(90, 1, 0, 0); 
    glTranslatef(0, 0, -2.0); 
    GLUquadric *q = gluNewQuadric();
    gluCylinder(q, 0.12, 0.12, 4.0, 16, 1);
    gluDeleteQuadric(q);
    glPopMatrix();
    
    /* 2. Swirling Sparkles - Increased visibility and count */
    for(int i=0; i<30; i++) {
        double angle = (pulse * 25.0) + (i * 12.0);
        double r = 0.15; /* Slightly wider than the beam */
        double px = x + cos(angle * M_PI / 180.0) * r;
        double pz = z + sin(angle * M_PI / 180.0) * r;
        /* Vertical distribution centered around the probe */
        double py = y - 1.5 + (i % 15) * 0.2;
        
        glColor4f(1.0, 1.0, 1.0, beam_alpha);
        glPushMatrix();
        glTranslatef(px, py, pz);
        glutSolidSphere(0.025, 4, 4); /* Made them larger */
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    g_recovery_fx.timer--;
}

void drawFaceLabels() {
    if (!g_show_hud || g_show_map) return;
    
    int q1 = g_my_q[0];
    int q2 = g_my_q[1];
    int q3 = g_my_q[2];

    struct {
        double x, y, z;
        int nq[3];
    } faces[6] = {
        { 20.5, 0, 0, {q1+1, q2, q3} },   /* Right (X+) */
        {-20.5, 0, 0, {q1-1, q2, q3} },   /* Left (X-) */
        { 0, 20.5, 0, {q1, q2, q3+1} },   /* Top (Z+) -> Y+ in viewer */
        { 0,-20.5, 0, {q1, q2, q3-1} },   /* Down (Z-) -> Y- in viewer */
        { 0, 0,-20.5, {q1, q2+1, q3} },   /* Fore (Y+) -> Z- in viewer */
        { 0, 0, 20.5, {q1, q2-1, q3} }    /* Aft (Y-) -> Z+ in viewer */
    };

    GLdouble model[16], proj[16];
    GLint view[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, view);

    for(int i=0; i<6; i++) {
        if (!IS_Q_VALID(faces[i].nq[0], faces[i].nq[1], faces[i].nq[2])) continue;
        
        GLdouble winX, winY, winZ;
        if (gluProject(faces[i].x, faces[i].y, faces[i].z, model, proj, view, &winX, &winY, &winZ) == GL_TRUE) {
            if (winZ < 0.0 || winZ > 1.0) continue;
            
            char buf[32];
            sprintf(buf, "[%d,%d,%d]", faces[i].nq[0], faces[i].nq[1], faces[i].nq[2]);
            
            glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
            gluOrtho2D(0, view[2], 0, view[3]);
            glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
            glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
            
            glColor3f(0.0, 0.8, 0.8); /* Cyan-ish LCARS */
            glRasterPos2f(winX - 25, winY);
            for(size_t k=0; k<strlen(buf); k++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, buf[k]);
            
            glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
            glMatrixMode(GL_PROJECTION); glPopMatrix();
            glMatrixMode(GL_MODELVIEW); glPopMatrix();
        }
    }
}

/* Tactical Grid VBO Globals */
GLuint vbo_tactical_grid = 0;
int tactical_grid_count = 0;

void realInitTacticalGrid() {
    struct Vertex { float x, y, z; float r, g, b; };
    struct Vertex verts[64]; /* Safe upper bound */
    int idx = 0;
    float min = -20.0f, mid = 0.0f, max = 20.0f;

    /* Top Loop (Green) */
    verts[idx++] = (struct Vertex){min, max, min, 0, 1, 0}; verts[idx++] = (struct Vertex){max, max, min, 0, 1, 0};
    verts[idx++] = (struct Vertex){max, max, min, 0, 1, 0}; verts[idx++] = (struct Vertex){max, max, max, 0, 1, 0};
    verts[idx++] = (struct Vertex){max, max, max, 0, 1, 0}; verts[idx++] = (struct Vertex){min, max, max, 0, 1, 0};
    verts[idx++] = (struct Vertex){min, max, max, 0, 1, 0}; verts[idx++] = (struct Vertex){min, max, min, 0, 1, 0};

    /* Bottom Loop (Red) */
    verts[idx++] = (struct Vertex){min, min, min, 1, 0, 0}; verts[idx++] = (struct Vertex){max, min, min, 1, 0, 0};
    verts[idx++] = (struct Vertex){max, min, min, 1, 0, 0}; verts[idx++] = (struct Vertex){max, min, max, 1, 0, 0};
    verts[idx++] = (struct Vertex){max, min, max, 1, 0, 0}; verts[idx++] = (struct Vertex){min, min, max, 1, 0, 0};
    verts[idx++] = (struct Vertex){min, min, max, 1, 0, 0}; verts[idx++] = (struct Vertex){min, min, min, 1, 0, 0};

    /* Verticals */
    float corners[4][2] = {{min, min}, {max, min}, {max, max}, {min, max}};
    for(int i=0; i<4; i++) {
        float x = corners[i][0];
        float z = corners[i][1];
        /* Top -> Mid (Green -> Yellow) */
        verts[idx++] = (struct Vertex){x, max, z, 0, 1, 0}; 
        verts[idx++] = (struct Vertex){x, mid, z, 1, 1, 0};
        /* Mid -> Bottom (Yellow -> Red) */
        verts[idx++] = (struct Vertex){x, mid, z, 1, 1, 0};
        verts[idx++] = (struct Vertex){x, min, z, 1, 0, 0};
    }
    
    tactical_grid_count = idx;

    glGenBuffers(1, &vbo_tactical_grid);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_tactical_grid);
    glBufferData(GL_ARRAY_BUFFER, sizeof(struct Vertex) * idx, verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void drawTacticalCube() {
    if (vbo_tactical_grid == 0) realInitTacticalGrid();

    glDisable(GL_LIGHTING);
    glLineWidth(1.0);
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo_tactical_grid);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    
    glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (void*)0);
    glColorPointer(3, GL_FLOAT, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glDrawArrays(GL_LINES, 0, tactical_grid_count);
    
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_LIGHTING);
}

void spawnParticle(double x, double y, double z, double vx, double vy, double vz, double r, double g, double b, double size, double life) {
    static int last_spawn_idx = 0;
    int search_idx = last_spawn_idx;

    bool found = false;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        int idx = (search_idx + i) % MAX_PARTICLES;
        if (!fx_particles[idx].active) {
            fx_particles[idx].x = x; fx_particles[idx].y = y; fx_particles[idx].z = z;
            fx_particles[idx].vx = vx; fx_particles[idx].vy = vy; fx_particles[idx].vz = vz;
            fx_particles[idx].r = r; fx_particles[idx].g = g; fx_particles[idx].b = b;
            fx_particles[idx].a = 1.0;
            fx_particles[idx].size = size;
            fx_particles[idx].life = life;
            fx_particles[idx].active = 1;
            last_spawn_idx = (idx + 1) % MAX_PARTICLES;
            found = true;
            break;
        }
    }
    
    /* Fallback: if buffer is full, force overwrite the oldest slot in the ring */
    if (!found) {
        int idx = last_spawn_idx;
        fx_particles[idx].x = x; fx_particles[idx].y = y; fx_particles[idx].z = z;
        fx_particles[idx].vx = vx; fx_particles[idx].vy = vy; fx_particles[idx].vz = vz;
        fx_particles[idx].r = r; fx_particles[idx].g = g; fx_particles[idx].b = b;
        fx_particles[idx].a = 1.0;
        fx_particles[idx].size = size;
        fx_particles[idx].life = life;
        fx_particles[idx].active = 1;
        last_spawn_idx = (idx + 1) % MAX_PARTICLES;
    }
}

void updateParticles() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (fx_particles[i].active) {
            fx_particles[i].x += fx_particles[i].vx;
            fx_particles[i].y += fx_particles[i].vy;
            fx_particles[i].z += fx_particles[i].vz;
            
            /* Remove downward drift (gravity) to prevent artificial trajectories */
            fx_particles[i].vx *= 0.98;
            fx_particles[i].vy *= 0.98;
            fx_particles[i].vz *= 0.98;

            /* Randomized decay for spark effect - Use a simple thread-safe approach */
            float decay = (0.02 + ((i % 100) / 2500.0)); 
            fx_particles[i].life -= decay; 
            fx_particles[i].a = fx_particles[i].life;
            if (fx_particles[i].life <= 0) fx_particles[i].active = 0;
        }
    }
}

void drawParticles() {
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUseProgram(particleShaderProgram);
    
    /* 1. Pack active particles into the vertex buffer */
    int active_count = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (fx_particles[i].active) {
            particle_vertex_buffer[active_count].x = (float)fx_particles[i].x;
            particle_vertex_buffer[active_count].y = (float)fx_particles[i].y;
            particle_vertex_buffer[active_count].z = (float)fx_particles[i].z;
            particle_vertex_buffer[active_count].r = (float)fx_particles[i].r;
            particle_vertex_buffer[active_count].g = (float)fx_particles[i].g;
            particle_vertex_buffer[active_count].b = (float)fx_particles[i].b;
            particle_vertex_buffer[active_count].a = (float)fx_particles[i].a;
            particle_vertex_buffer[active_count].size = (float)fx_particles[i].size;
            active_count++;
        }
    }
    
    if (active_count > 0) {
        /* 2. Upload and Draw Batch */
        glPointSize(5.0);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_particles);
        glBufferSubData(GL_ARRAY_BUFFER, 0, active_count * sizeof(ParticleVertex), particle_vertex_buffer);
        
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        
        /* Strided pointer mapping for Batch Data */
        glVertexPointer(3, GL_FLOAT, sizeof(ParticleVertex), (void*)offsetof(ParticleVertex, x));
        glColorPointer(4, GL_FLOAT, sizeof(ParticleVertex), (void*)offsetof(ParticleVertex, r));
        
        GLint sizeLoc = glGetAttribLocation(particleShaderProgram, "pSize");
        glEnableVertexAttribArray(sizeLoc);
        glVertexAttribPointer(sizeLoc, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void*)offsetof(ParticleVertex, size));
        
        glDrawArrays(GL_POINTS, 0, active_count);
        
        glDisableVertexAttribArray(sizeLoc);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    
    glUseProgram(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void drawSkybox() {
    glDisable(GL_CULL_FACE); /* Ensure skybox is visible from inside */
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glUseProgram(skyboxShaderProgram);
    glUniform1f(glGetUniformLocation(skyboxShaderProgram, "time"), pulse);
    
    glPushMatrix();
    /* Rotate skybox slowly */
    glRotatef(pulse * 0.5, 0, 1, 0);
    glutSolidSphere(400.0, 32, 32);
    glPopMatrix();
    
    glUseProgram(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
    glEnable(GL_CULL_FACE);
}

void drawShieldEffect() {
    /* Shield Hit Detection & Synchronization */
    static int g_last_shields_val_hit[6] = {0};
    static bool g_shields_init = false;
    
    if (!g_shields_init) {
        for(int s=0; s<6; s++) g_last_shields_val_hit[s] = g_shm->buffers[g_shm->read_index].shm_shields[s];
        g_shields_init = true;
    }

    for(int s=0; s<6; s++) {
        if (g_shm->buffers[g_shm->read_index].shm_shields[s] < g_last_shields_val_hit[s]) {
            g_shield_hit_timers[s] = (int)(GAME_TICK_RATE); 
        }
        g_last_shields_val_hit[s] = g_shm->buffers[g_shm->read_index].shm_shields[s];
    }

    bool any_hit = false;
    for(int s=0; s<6; s++) if (g_shield_hit_timers[s] > 0) any_hit = true;
    if (!any_hit) return;
    
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    glPushMatrix();
    glTranslatef(PlayerX, PlayerY, PlayerZ);
    glRotatef(objects[0].h - 90.0f, 0, 1, 0);
    glRotatef(objects[0].m, 0, 0, 1);
    glRotatef(objects[0].r, 1, 0, 0);

    /* Shield sectors mapping: 0:F, 1:R, 2:T, 3:B, 4:L, 5:R
       Since ship points to +X in local model space, we rotate the Z-axis (0,0,1.2) to each face. */
    struct { double rx, ry; } shield_rot[] = {
        {0, 90},    /* 0: Front (+X) */
        {0, -90},   /* 1: Rear (-X) */
        {-90, 0},   /* 2: Top (+Y) */
        {90, 0},    /* 3: Bottom (-Y) */
        {0, 0},     /* 4: Left (+Z) */
        {0, 180}    /* 5: Right (-Z) */
    };

    for(int s=0; s<6; s++) {
        if (g_shield_hit_timers[s] <= 0) continue;
        
        double t = g_shield_hit_timers[s] / (double)GAME_TICK_RATE;
        double alpha = (t > 0.5) ? 1.0 : t * 2.0;
        double scale = 1.0 + (1.0 - t) * 0.15; 
        
        glPushMatrix();
        glRotatef(shield_rot[s].ry, 0, 1, 0);
        glRotatef(shield_rot[s].rx, 1, 0, 0);
        
        glTranslatef(0, 0, 1.25);
        glScalef(scale * 1.5, scale * 1.2, scale * 0.5);
        
        glColor4f(0.0, 0.7, 1.0, alpha * 0.7);
        
        /* Use a wireframe torus or sphere segment for the "grid" look */
        glutWireTorus(0.05, 0.8, 8, 12);
        
        /* Local glow at impact site */
        drawGlow(0.6, 0.0, 0.5, 1.0, alpha * 0.4);
        
        glPopMatrix();
    }
    
    glPopMatrix();
    glPopAttrib();
}

