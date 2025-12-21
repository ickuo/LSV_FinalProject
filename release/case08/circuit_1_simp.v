// Benchmark "top" written by ABC on Sun Dec 21 18:10:01 2025

module top ( 
    n16, n23, n24, n31, n38, n41, n44, n46, n53, n55, n57, n62, n68, n71,
    n72, n79, n84, n92, n96, n98, n101, n106, n107, n120, n123, n136, n145,
    n165, n177, n182, n216, n220,
    n5, n63, n78, n126, n142, n144, n153  );
  input  n16, n23, n24, n31, n38, n41, n44, n46, n53, n55, n57, n62, n68,
    n71, n72, n79, n84, n92, n96, n98, n101, n106, n107, n120, n123, n136,
    n145, n165, n177, n182, n216, n220;
  output n5, n63, n78, n126, n142, n144, n153;
  wire new_n40, new_n41_1, new_n42, new_n43, new_n44_1, new_n45, new_n46_1,
    new_n47, new_n48, new_n49, new_n50, new_n51, new_n52, new_n53_1,
    new_n55_1, new_n56, new_n57_1, new_n58, new_n59, new_n60, new_n61,
    new_n62_1, new_n63_1, new_n64, new_n65, new_n66, new_n67, new_n68_1,
    new_n69, new_n70, new_n71_1, new_n72_1, new_n73, new_n74, new_n75,
    new_n76, new_n77, new_n78_1, new_n79_1, new_n80, new_n81, new_n82,
    new_n83, new_n84_1, new_n85, new_n87, new_n88, new_n89, new_n90,
    new_n91, new_n92_1, new_n93, new_n94, new_n95, new_n96_1, new_n97,
    new_n98_1, new_n99, new_n100, new_n101_1, new_n102, new_n103, new_n104,
    new_n105, new_n106_1, new_n107_1, new_n108, new_n109, new_n110,
    new_n111, new_n112, new_n113, new_n114, new_n115, new_n116, new_n117,
    new_n118, new_n119, new_n121, new_n122, new_n123_1, new_n124, new_n125,
    new_n126_1, new_n127, new_n128, new_n129, new_n130, new_n131, new_n132,
    new_n133, new_n134, new_n135, new_n136_1, new_n137, new_n138, new_n139,
    new_n140, new_n141, new_n142_1, new_n143, new_n144_1, new_n145_1,
    new_n146, new_n147, new_n148, new_n149, new_n151, new_n152, new_n154,
    new_n155, new_n157, new_n158, new_n159, new_n160, new_n161, new_n162,
    new_n163;
  assign new_n40 = n55 & ~n165;
  assign new_n41_1 = n38 & ~n71;
  assign new_n42 = ~new_n40 & ~new_n41_1;
  assign new_n43 = n96 & ~n98;
  assign new_n44_1 = ~n44 & n53;
  assign new_n45 = ~new_n43 & ~new_n44_1;
  assign new_n46_1 = new_n42 & new_n45;
  assign new_n47 = n84 & ~n101;
  assign new_n48 = ~n46 & n182;
  assign new_n49 = ~new_n47 & ~new_n48;
  assign new_n50 = ~n57 & n68;
  assign new_n51 = ~n41 & n84;
  assign new_n52 = ~new_n50 & ~new_n51;
  assign new_n53_1 = new_n49 & new_n52;
  assign n5 = ~new_n46_1 | ~new_n53_1;
  assign new_n55_1 = n98 & n5;
  assign new_n56 = n96 & ~new_n55_1;
  assign new_n57_1 = ~n72 & new_n56;
  assign new_n58 = n165 & n5;
  assign new_n59 = n55 & ~new_n58;
  assign new_n60 = ~n79 & new_n59;
  assign new_n61 = ~new_n57_1 & ~new_n60;
  assign new_n62_1 = n46 & n5;
  assign new_n63_1 = n182 & ~new_n62_1;
  assign new_n64 = ~n123 & new_n63_1;
  assign new_n65 = n84 & ~n5;
  assign new_n66 = ~new_n51 & ~new_n65;
  assign new_n67 = n145 & ~n5;
  assign new_n68_1 = ~n92 & new_n67;
  assign new_n69 = new_n66 & ~new_n68_1;
  assign new_n70 = ~new_n64 & new_n69;
  assign new_n71_1 = new_n61 & new_n70;
  assign new_n72_1 = n44 & n5;
  assign new_n73 = n53 & ~new_n72_1;
  assign new_n74 = ~n136 & new_n73;
  assign new_n75 = n71 & n5;
  assign new_n76 = n38 & ~new_n75;
  assign new_n77 = ~n23 & new_n76;
  assign new_n78_1 = ~new_n74 & ~new_n77;
  assign new_n79_1 = n57 & n5;
  assign new_n80 = n68 & ~new_n79_1;
  assign new_n81 = ~n107 & new_n80;
  assign new_n82 = ~new_n47 & ~new_n65;
  assign new_n83 = ~n120 & ~new_n82;
  assign new_n84_1 = ~new_n81 & ~new_n83;
  assign new_n85 = new_n78_1 & new_n84_1;
  assign n153 = ~new_n71_1 | ~new_n85;
  assign new_n87 = n72 & n153;
  assign new_n88 = new_n56 & ~new_n87;
  assign new_n89 = ~n62 & new_n88;
  assign new_n90 = ~n31 & new_n83;
  assign new_n91 = ~n106 & ~new_n66;
  assign new_n92_1 = n24 & new_n68_1;
  assign new_n93 = ~new_n91 & ~new_n92_1;
  assign new_n94 = ~n16 & new_n60;
  assign new_n95 = new_n93 & ~new_n94;
  assign new_n96_1 = ~new_n90 & new_n95;
  assign new_n97 = ~n177 & new_n73;
  assign new_n98_1 = ~n136 & new_n97;
  assign new_n99 = new_n96_1 & ~new_n98_1;
  assign new_n100 = ~n220 & new_n63_1;
  assign new_n101_1 = n24 & new_n67;
  assign new_n102 = ~new_n100 & ~new_n101_1;
  assign new_n103 = ~n84 & new_n76;
  assign new_n104 = ~n16 & new_n59;
  assign new_n105 = ~new_n103 & ~new_n104;
  assign new_n106_1 = ~n31 & ~new_n82;
  assign new_n107_1 = ~n216 & new_n80;
  assign new_n108 = ~new_n106_1 & ~new_n107_1;
  assign new_n109 = new_n105 & new_n108;
  assign new_n110 = new_n102 & new_n109;
  assign new_n111 = ~new_n97 & new_n110;
  assign new_n112 = ~n153 & ~new_n111;
  assign new_n113 = ~n84 & new_n77;
  assign new_n114 = ~n107 & new_n107_1;
  assign new_n115 = ~new_n113 & ~new_n114;
  assign new_n116 = ~n123 & new_n100;
  assign new_n117 = new_n115 & ~new_n116;
  assign new_n118 = ~new_n112 & new_n117;
  assign new_n119 = new_n99 & new_n118;
  assign n142 = new_n89 | ~new_n119;
  assign new_n121 = n31 & n142;
  assign new_n122 = n120 & n153;
  assign new_n123_1 = ~new_n82 & ~new_n122;
  assign new_n124 = ~new_n121 & new_n123_1;
  assign new_n125 = n16 & n142;
  assign new_n126_1 = n79 & n153;
  assign new_n127 = new_n59 & ~new_n126_1;
  assign new_n128 = ~new_n125 & new_n127;
  assign new_n129 = n177 & n142;
  assign new_n130 = n136 & n153;
  assign new_n131 = new_n73 & ~new_n130;
  assign new_n132 = ~new_n129 & new_n131;
  assign new_n133 = new_n128 & ~new_n132;
  assign new_n134 = n216 & n142;
  assign new_n135 = n107 & n153;
  assign new_n136_1 = new_n80 & ~new_n135;
  assign new_n137 = ~new_n134 & new_n136_1;
  assign new_n138 = ~n24 & n142;
  assign new_n139 = n92 & n153;
  assign new_n140 = new_n67 & ~new_n139;
  assign new_n141 = ~new_n138 & new_n140;
  assign new_n142_1 = new_n137 & ~new_n141;
  assign new_n143 = n62 & ~new_n119;
  assign new_n144_1 = new_n88 & ~new_n143;
  assign new_n145_1 = ~new_n142_1 & ~new_n144_1;
  assign new_n146 = ~new_n133 & new_n145_1;
  assign new_n147 = ~new_n66 & ~n142;
  assign new_n148 = ~new_n91 & ~new_n147;
  assign new_n149 = ~new_n146 & new_n148;
  assign n63 = ~new_n124 & ~new_n149;
  assign new_n151 = ~new_n124 & new_n148;
  assign new_n152 = ~new_n141 & ~new_n144_1;
  assign n78 = ~new_n151 | ~new_n152;
  assign new_n154 = ~new_n132 & ~new_n137;
  assign new_n155 = new_n152 & ~new_n154;
  assign n126 = new_n151 & ~new_n155;
  assign new_n157 = ~n182 & ~new_n128;
  assign new_n158 = new_n154 & new_n157;
  assign new_n159 = ~n78 & new_n158;
  assign new_n160 = n84 & n142;
  assign new_n161 = n23 & n153;
  assign new_n162 = new_n76 & ~new_n161;
  assign new_n163 = ~new_n160 & new_n162;
  assign n144 = ~new_n159 & ~new_n163;
endmodule


