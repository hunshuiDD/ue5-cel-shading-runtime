// =====================================================================
//  MF_PerShadow — Custom 节点配置说明（逻辑在 PerShadow.ush）
//  Output Type : CMOT Float1   (DebugMode=0 返回 shadow，其余模式返回诊断标量)
//
//  【关键】UE 会把 Code 框内容塞进一个函数体内部，所以 #include 不能写在
//  Code 框里（否则 PerShadow.ush 的函数定义会被嵌进函数里，报
//  "function definition is not allowed here"）。正确分两处：
//
//  1) Custom 节点 Details 面板 -> Include File Paths 数组 -> 加【一行】：
//        /PreObjectShadow/PerShadow.ush
//     （确保没有多余空行；空行会导致 File '' not found）
//
//  2) Code 框里【只放下面这一行调用】，不要写 #include：
//        return PerShadow(Tex, TexSampler, WorldPos, N, L, CamPos, Right, Up, Fwd, OrthoWidth, Reso, ConstBias, SlopeBias, NormalBias, Mode, SoftRadius, LightSize, DebugMode, ScreenPos);
//
//  输入引脚共 18 个（TexSampler 由 Tex 自动生成；其余名字大小写一字不差）：
//   Tex Texture Object -> RT_Daniya_Pershadow (R32f, SceneDepth)
//   WorldPos=AbsWorldPos, N=PixelNormalWS, L=MPC.ShadowLightDir,
//   CamPos/Right/Up/Fwd/OrthoWidth/Reso 取自 MPC，
//   ConstBias/SlopeBias/NormalBias/Mode/SoftRadius/LightSize 为材质参数，
//   DebugMode = MCP_CharaterRuning.ShadowDebugMode（0=正常，1..6=内部诊断），
//   ScreenPos = ScreenPosition 节点的 【PixelPosition】 输出（Poisson 每像素旋转抖动用）。
//   ★ 注意：ScreenPos 必须接 PixelPosition，不能接 ViewportUV(0~1)——
//     接错不会报错，只会让抖动静默失效，半影区依旧出现规则条带。
//
//  报错会指向 PerShadow.ush 真实行号；改完 `recompileshaders changed`。
// =====================================================================

// —— 只把下面这一行粘进 Custom 节点的 Code 框 ——
return PerShadow(Tex, TexSampler, WorldPos, N, L, CamPos, Right, Up, Fwd, OrthoWidth, Reso, ConstBias, SlopeBias, NormalBias, Mode, SoftRadius, LightSize, DebugMode, ScreenPos);
