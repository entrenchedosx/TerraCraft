import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.math.Axis;
import net.minecraft.util.Mth;
import org.joml.Matrix4f;
import org.joml.Quaternionf;

import java.io.PrintStream;

/**
 * Reference-math generator for TerraCraft's first-person hand tests.
 *
 * Replays the PoseStack call sequences found in the installed game's
 * FirstPersonHandsAndItemsRenderer / GameRenderer.bobView / ModelPart bytecode
 * (constants verified with `javap -c`, see docs/VANILLA_REFERENCE_METHOD.md)
 * through the game's own PoseStack, Axis and Mth classes, and prints the
 * resulting matrices as a C header. This is a developer-only utility: it needs
 * a locally installed game jar on the classpath and is NOT part of the shipped
 * game or the default test run.
 *
 * Usage: java -cp <game.jar>;<joml.jar>;... HandGolden > tests/fixtures/hand_golden.h
 */
public class HandGolden {
    static final float PI = (float) Math.PI;

    static String fmt(Matrix4f m) {
        float[] a = new float[16];
        m.get(a); // column-major, same layout as Mat4
        StringBuilder sb = new StringBuilder("{");
        for (int i = 0; i < 16; i++) {
            if (i > 0) sb.append(", ");
            sb.append(String.format("%.9ef", a[i]));
        }
        return sb.append("}").toString();
    }

    /** renderPlayerArm for the right arm, then the arm ModelPart pose. */
    static Matrix4f arm(float swing, float equipped) {
        PoseStack ps = new PoseStack();
        float f = 1.0f;
        float f1 = Mth.sqrt(swing);
        float f2 = -0.3f * Mth.sin((double) (f1 * PI));
        float f3 = 0.4f * Mth.sin((double) (f1 * 6.2831855f));
        float f4 = -0.4f * Mth.sin((double) (swing * PI));
        ps.translate(f * (f2 + 0.64000005f), f3 + -0.6f + equipped * -0.6f, f4 + -0.71999997f);
        ps.rotateDegrees(Axis.YP, f * 45.0f);
        float f5 = Mth.sin((double) (swing * swing * PI));
        float f6 = Mth.sin((double) (f1 * PI));
        ps.rotateDegrees(Axis.YP, f * f6 * 70.0f);
        ps.rotateDegrees(Axis.ZP, f * f5 * -20.0f);
        ps.translate(f * -1.0f, 3.6f, 3.5f);
        ps.rotateDegrees(Axis.ZP, f * 120.0f);
        ps.rotateDegrees(Axis.XP, 200.0f);
        ps.rotateDegrees(Axis.YP, f * -135.0f);
        ps.translate(f * 5.6f, 0.0f, 0.0f);
        return new Matrix4f(ps.last().pose());
    }

    /** ModelPart.translateAndRotate for right_arm after renderHand (zRot = 0.1). */
    static Matrix4f armPart() {
        PoseStack ps = new PoseStack();
        ps.translate(-5.0f / 16.0f, 2.0f / 16.0f, 0.0f);
        ps.rotate(new Quaternionf().rotationZYX(0.1f, 0.0f, 0.0f));
        return new Matrix4f(ps.last().pose());
    }

    /** Generic held-item branch (not using): swing translate + arm + attack transforms. */
    static Matrix4f itemSwing(float swing, float equipped) {
        PoseStack ps = new PoseStack();
        float f5 = -0.4f * Mth.sin((double) (Mth.sqrt(swing) * PI));
        float f6 = 0.2f * Mth.sin((double) (Mth.sqrt(swing) * 6.2831855f));
        float f10 = -0.2f * Mth.sin((double) (swing * PI));
        int l = 1;
        ps.translate(l * f5, f6, f10);
        ps.translate(l * 0.56f, -0.52f + equipped * -0.6f, -0.72f);
        float f = Mth.sin((double) (swing * swing * PI));
        ps.rotateDegrees(Axis.YP, l * (45.0f + f * -20.0f));
        float f1 = Mth.sin((double) (Mth.sqrt(swing) * PI));
        ps.rotateDegrees(Axis.ZP, l * f1 * -20.0f);
        ps.rotateDegrees(Axis.XP, f1 * -80.0f);
        ps.rotateDegrees(Axis.YP, l * -45.0f);
        return new Matrix4f(ps.last().pose());
    }

    /** applyEatTransform followed by applyItemArmTransform. */
    static Matrix4f eat(float partial, float remaining, float duration, float equipped) {
        PoseStack ps = new PoseStack();
        float f = remaining - partial + 1.0f;
        float f1 = f / duration;
        if (f1 < 0.8f) {
            float f2 = Mth.abs(Mth.cos((double) (f / 4.0f * PI)) * 0.1f);
            ps.translate(0.0f, f2, 0.0f);
        }
        float f3 = 1.0f - (float) Math.pow((double) f1, 27.0);
        int i = 1;
        ps.translate(f3 * 0.6f * i, f3 * -0.5f, f3 * 0.0f);
        ps.rotateDegrees(Axis.YP, i * f3 * 90.0f);
        ps.rotateDegrees(Axis.XP, f3 * 10.0f);
        ps.rotateDegrees(Axis.ZP, i * f3 * 30.0f);
        ps.translate(i * 0.56f, -0.52f + equipped * -0.6f, -0.72f);
        return new Matrix4f(ps.last().pose());
    }

    /** GameRenderer.bobView. */
    static Matrix4f bobView(float walkDist, float walkDistO, float oBob, float bob, float partial) {
        PoseStack ps = new PoseStack();
        float f = walkDist - walkDistO;
        float f1 = -(walkDist + f * partial);
        float f2 = Mth.lerp(partial, oBob, bob);
        ps.translate(Mth.sin((double) (f1 * PI)) * f2 * 0.5f,
                -Math.abs(Mth.cos((double) (f1 * PI)) * f2), 0.0f);
        ps.rotateDegrees(Axis.ZP, Mth.sin((double) (f1 * PI)) * f2 * 3.0f);
        ps.rotateDegrees(Axis.XP, Math.abs(Mth.cos((double) (f1 * PI - 0.2f)) * f2) * 5.0f);
        return new Matrix4f(ps.last().pose());
    }

    /** ItemTransform.apply for firstperson_righthand + model centring. */
    static Matrix4f display(float rx, float ry, float rz, float tx, float ty, float tz, float sc) {
        PoseStack ps = new PoseStack();
        float d = 0.017453292f;
        ps.translate(tx / 16.0f, ty / 16.0f, tz / 16.0f);
        ps.rotate(new Quaternionf().rotationXYZ(rx * d, ry * d, rz * d));
        ps.scale(sc, sc, sc);
        ps.translate(-0.5f, -0.5f, -0.5f);
        return new Matrix4f(ps.last().pose());
    }

    public static void main(String[] args) {
        PrintStream o = System.out;
        o.println("/* Generated by tools/reference/HandGolden.java from the installed game's");
        o.println(" * PoseStack/Axis/Mth classes. Do not edit by hand. Column-major (Mat4 layout). */");
        o.println("#pragma once");
        o.println();
        float[] swings = {0.0f, 1.0f / 6.0f, 0.25f, 0.5f, 5.0f / 6.0f, 1.0f};
        float[] equips = {0.0f, 0.5f, 1.0f};
        o.println("typedef struct HandGoldenArm { float swing, equipped; float m[16]; } HandGoldenArm;");
        o.println("static const HandGoldenArm HAND_GOLDEN_ARM[] = {");
        for (float s : swings) for (float e : equips)
            o.printf("    {%.9ef, %.9ef, %s},%n", s, e, fmt(arm(s, e)));
        o.println("};");
        o.println("typedef struct HandGoldenItem { float swing, equipped; float m[16]; } HandGoldenItem;");
        o.println("static const HandGoldenItem HAND_GOLDEN_ITEM[] = {");
        for (float s : swings) for (float e : equips)
            o.printf("    {%.9ef, %.9ef, %s},%n", s, e, fmt(itemSwing(s, e)));
        o.println("};");
        o.println("static const float HAND_GOLDEN_ARM_PART[16] = " + fmt(armPart()) + ";");
        o.println("typedef struct HandGoldenEat { float partial, remaining, duration, equipped; float m[16]; } HandGoldenEat;");
        o.println("static const HandGoldenEat HAND_GOLDEN_EAT[] = {");
        float[][] eats = {{0.0f, 32.0f, 32.0f, 0.0f}, {0.5f, 20.0f, 32.0f, 0.0f}, {0.25f, 6.0f, 32.0f, 0.0f},
                {0.0f, 1.0f, 32.0f, 0.0f}, {0.75f, 3.0f, 32.0f, 0.3f}};
        for (float[] e : eats)
            o.printf("    {%.9ef, %.9ef, %.9ef, %.9ef, %s},%n", e[0], e[1], e[2], e[3], fmt(eatMat(e)));
        o.println("};");
        o.println("typedef struct HandGoldenBob { float walk, walk_o, o_bob, bob, partial; float m[16]; } HandGoldenBob;");
        o.println("static const HandGoldenBob HAND_GOLDEN_BOB[] = {");
        float[][] bobs = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {1.23f, 0.9f, 0.08f, 0.1f, 0.5f},
                {10.4f, 10.1f, 0.1f, 0.1f, 0.0f}, {10.4f, 10.1f, 0.1f, 0.1f, 1.0f}, {55.55f, 55.2f, 0.05f, 0.1f, 0.3f}};
        for (float[] b : bobs)
            o.printf("    {%.9ef, %.9ef, %.9ef, %.9ef, %.9ef, %s},%n", b[0], b[1], b[2], b[3], b[4],
                    fmt(bobView(b[0], b[1], b[2], b[3], b[4])));
        o.println("};");
        o.println("static const float HAND_GOLDEN_DISPLAY_SPRITE[16] = "
                + fmt(display(0.0f, -90.0f, 25.0f, 1.13f, 3.2f, 1.13f, 0.68f)) + ";");
        o.println("static const float HAND_GOLDEN_DISPLAY_BLOCK[16] = "
                + fmt(display(0.0f, 45.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.4f)) + ";");
    }

    static Matrix4f eatMat(float[] e) {
        return eat(e[0], e[1], e[2], e[3]);
    }
}
