// Benchmark "top" written by ABC on Sun Dec 21 18:10:18 2025

module top ( 
    n19, n24, n32, n36, n49, n52, n58, n59, n60, n69, n73, n96, n97, n100,
    n108, n117, n123, n143, n149, n151, n158, n167, n189, n196, n197, n198,
    n206, n209, n210, n217, n218, n222, n226, n233, n242, n258,
    n29, n76, n152, n194, n224, n245, n248  );
  input  n19, n24, n32, n36, n49, n52, n58, n59, n60, n69, n73, n96, n97,
    n100, n108, n117, n123, n143, n149, n151, n158, n167, n189, n196, n197,
    n198, n206, n209, n210, n217, n218, n222, n226, n233, n242, n258;
  output n29, n76, n152, n194, n224, n245, n248;
  wire new_n44, new_n45, new_n46, new_n47, new_n48, new_n49_1, new_n50,
    new_n51, new_n52_1, new_n53, new_n54, new_n55, new_n56, new_n57,
    new_n58_1, new_n59_1, new_n61, new_n62, new_n63, new_n64, new_n65,
    new_n66, new_n67, new_n68, new_n69_1, new_n70, new_n71, new_n72,
    new_n73_1, new_n74, new_n75, new_n76_1, new_n77, new_n78, new_n79,
    new_n80, new_n81, new_n82, new_n83, new_n84, new_n85, new_n86, new_n87,
    new_n88, new_n89, new_n90, new_n91, new_n92, new_n93, new_n94,
    new_n96_1, new_n97_1, new_n98, new_n99, new_n100_1, new_n101, new_n102,
    new_n103, new_n104, new_n105, new_n106, new_n107, new_n108_1, new_n109,
    new_n110, new_n111, new_n112, new_n113, new_n114, new_n115, new_n116,
    new_n117_1, new_n118, new_n119, new_n120, new_n121, new_n122,
    new_n123_1, new_n124, new_n125, new_n126, new_n127, new_n128, new_n129,
    new_n130, new_n131, new_n133, new_n134, new_n135, new_n136, new_n137,
    new_n138, new_n139, new_n140, new_n141, new_n142, new_n143_1, new_n144,
    new_n145, new_n146, new_n147, new_n148, new_n149_1, new_n150,
    new_n151_1, new_n152_1, new_n153, new_n154, new_n155, new_n157,
    new_n158_1, new_n159, new_n160, new_n161, new_n163, new_n164, new_n165;
  assign new_n44 = ~n24 & n100;
  assign new_n45 = ~n210 & n217;
  assign new_n46 = ~new_n44 & ~new_n45;
  assign new_n47 = ~n69 & n73;
  assign new_n48 = ~n97 & n123;
  assign new_n49_1 = ~new_n47 & ~new_n48;
  assign new_n50 = ~n218 & n258;
  assign new_n51 = n59 & ~n151;
  assign new_n52_1 = ~new_n50 & ~new_n51;
  assign new_n53 = new_n49_1 & new_n52_1;
  assign new_n54 = ~n58 & n149;
  assign new_n55 = ~n108 & n209;
  assign new_n56 = n117 & ~n233;
  assign new_n57 = ~new_n55 & ~new_n56;
  assign new_n58_1 = ~new_n54 & new_n57;
  assign new_n59_1 = new_n53 & new_n58_1;
  assign n29 = ~new_n46 | ~new_n59_1;
  assign new_n61 = n210 & n29;
  assign new_n62 = n217 & ~new_n61;
  assign new_n63 = n151 & n29;
  assign new_n64 = n59 & ~new_n63;
  assign new_n65 = ~n197 & new_n64;
  assign new_n66 = n24 & n29;
  assign new_n67 = n100 & ~new_n66;
  assign new_n68 = ~n60 & new_n67;
  assign new_n69_1 = ~new_n65 & ~new_n68;
  assign new_n70 = n58 & n29;
  assign new_n71 = n149 & ~new_n70;
  assign new_n72 = ~n222 & new_n71;
  assign new_n73_1 = n218 & n29;
  assign new_n74 = n258 & ~new_n73_1;
  assign new_n75 = ~n189 & new_n74;
  assign new_n76_1 = ~new_n72 & ~new_n75;
  assign new_n77 = n108 & n29;
  assign new_n78 = n209 & ~new_n77;
  assign new_n79 = ~n52 & new_n78;
  assign new_n80 = n233 & n29;
  assign new_n81 = n117 & ~new_n80;
  assign new_n82 = ~n196 & new_n81;
  assign new_n83 = ~new_n79 & ~new_n82;
  assign new_n84 = new_n76_1 & new_n83;
  assign new_n85 = n69 & n29;
  assign new_n86 = n73 & ~new_n85;
  assign new_n87 = ~n242 & new_n86;
  assign new_n88 = ~n158 & new_n62;
  assign new_n89 = n97 & n29;
  assign new_n90 = n123 & ~new_n89;
  assign new_n91 = ~n206 & new_n90;
  assign new_n92 = ~new_n88 & ~new_n91;
  assign new_n93 = ~new_n87 & new_n92;
  assign new_n94 = new_n84 & new_n93;
  assign n245 = ~new_n69_1 | ~new_n94;
  assign new_n96_1 = n158 & n245;
  assign new_n97_1 = new_n62 & ~new_n96_1;
  assign new_n98 = n60 & n245;
  assign new_n99 = new_n67 & ~new_n98;
  assign new_n100_1 = ~n167 & new_n99;
  assign new_n101 = n197 & n245;
  assign new_n102 = new_n64 & ~new_n101;
  assign new_n103 = ~n143 & new_n102;
  assign new_n104 = ~new_n100_1 & ~new_n103;
  assign new_n105 = n52 & n245;
  assign new_n106 = new_n78 & ~new_n105;
  assign new_n107 = ~n96 & new_n106;
  assign new_n108_1 = ~n19 & new_n97_1;
  assign new_n109 = ~new_n107 & ~new_n108_1;
  assign new_n110 = n189 & n245;
  assign new_n111 = new_n74 & ~new_n110;
  assign new_n112 = ~n32 & new_n111;
  assign new_n113 = n242 & n245;
  assign new_n114 = new_n86 & ~new_n113;
  assign new_n115 = ~n198 & new_n114;
  assign new_n116 = ~new_n112 & ~new_n115;
  assign new_n117_1 = new_n109 & new_n116;
  assign new_n118 = n206 & n245;
  assign new_n119 = new_n90 & ~new_n118;
  assign new_n120 = ~n226 & new_n119;
  assign new_n121 = n222 & n245;
  assign new_n122 = new_n71 & ~new_n121;
  assign new_n123_1 = ~n49 & new_n122;
  assign new_n124 = ~new_n120 & ~new_n123_1;
  assign new_n125 = ~n36 & new_n81;
  assign new_n126 = n196 & ~n245;
  assign new_n127 = ~n196 & n245;
  assign new_n128 = ~new_n126 & ~new_n127;
  assign new_n129 = new_n125 & ~new_n128;
  assign new_n130 = new_n124 & ~new_n129;
  assign new_n131 = new_n117_1 & new_n130;
  assign n194 = ~new_n104 | ~new_n131;
  assign new_n133 = n19 & n194;
  assign new_n134 = new_n97_1 & ~new_n133;
  assign new_n135 = n167 & n194;
  assign new_n136 = new_n99 & ~new_n135;
  assign new_n137 = ~new_n134 & ~new_n136;
  assign new_n138 = n143 & n194;
  assign new_n139 = new_n102 & ~new_n138;
  assign new_n140 = n49 & n194;
  assign new_n141 = new_n122 & ~new_n140;
  assign new_n142 = n32 & n194;
  assign new_n143_1 = new_n111 & ~new_n142;
  assign new_n144 = ~new_n141 & ~new_n143_1;
  assign new_n145 = ~new_n139 & new_n144;
  assign new_n146 = n96 & n194;
  assign new_n147 = new_n106 & ~new_n146;
  assign new_n148 = n226 & n194;
  assign new_n149_1 = new_n119 & ~new_n148;
  assign new_n150 = ~new_n147 & ~new_n149_1;
  assign new_n151_1 = ~n117 & new_n150;
  assign new_n152_1 = new_n145 & new_n151_1;
  assign new_n153 = new_n137 & new_n152_1;
  assign new_n154 = n198 & n194;
  assign new_n155 = new_n114 & ~new_n154;
  assign n76 = ~new_n153 & ~new_n155;
  assign new_n157 = ~new_n136 & new_n143_1;
  assign new_n158_1 = ~new_n134 & ~new_n157;
  assign new_n159 = ~new_n147 & ~new_n158_1;
  assign new_n160 = new_n141 & new_n150;
  assign new_n161 = ~new_n159 & ~new_n160;
  assign n152 = new_n139 | ~new_n161;
  assign new_n163 = ~new_n139 & ~new_n147;
  assign new_n164 = ~new_n143_1 & ~new_n149_1;
  assign new_n165 = new_n137 & ~new_n164;
  assign n224 = ~new_n163 | new_n165;
  assign n248 = ~new_n137 | ~new_n163;
endmodule


