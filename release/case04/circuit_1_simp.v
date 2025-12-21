// Benchmark "top" written by ABC on Sun Dec 21 18:06:53 2025

module top ( 
    n1, n4, n10, n13, n27, n32, n46, n58, n61, n64, n65, n66,
    n26, n34, n52, n54  );
  input  n1, n4, n10, n13, n27, n32, n46, n58, n61, n64, n65, n66;
  output n26, n34, n52, n54;
  wire new_n17, new_n18, new_n19, new_n20, new_n21, new_n22, new_n23,
    new_n24, new_n25, new_n26_1, new_n27_1, new_n28, new_n29, new_n31,
    new_n32_1, new_n33, new_n34_1, new_n35, new_n36, new_n37, new_n38,
    new_n39, new_n40, new_n41, new_n42, new_n43, new_n44, new_n45,
    new_n46_1, new_n47, new_n48, new_n50, new_n51, new_n52_1, new_n53,
    new_n54_1, new_n55, new_n56, new_n57, new_n58_1, new_n59, new_n60,
    new_n61_1, new_n62, new_n63, new_n64_1, new_n65_1, new_n66_1, new_n67,
    new_n68, new_n69, new_n70, new_n71, new_n72, new_n73, new_n74, new_n75,
    new_n76, new_n77, new_n79;
  assign new_n17 = ~n32 & ~n61;
  assign new_n18 = ~n13 & ~n66;
  assign new_n19 = ~new_n17 & ~new_n18;
  assign new_n20 = ~n61 & ~n66;
  assign new_n21 = ~n13 & ~n32;
  assign new_n22 = new_n20 & new_n21;
  assign new_n23 = ~new_n19 & ~new_n22;
  assign new_n24 = ~n4 & new_n20;
  assign new_n25 = n1 & ~new_n24;
  assign new_n26_1 = ~n1 & new_n24;
  assign new_n27_1 = ~new_n25 & ~new_n26_1;
  assign new_n28 = ~new_n23 & ~new_n27_1;
  assign new_n29 = new_n23 & new_n27_1;
  assign n26 = ~new_n28 & ~new_n29;
  assign new_n31 = n10 & ~n61;
  assign new_n32_1 = n65 & ~n66;
  assign new_n33 = ~new_n21 & ~new_n32_1;
  assign new_n34_1 = new_n21 & new_n32_1;
  assign new_n35 = ~new_n33 & ~new_n34_1;
  assign new_n36 = new_n31 & ~new_n35;
  assign new_n37 = ~new_n31 & new_n35;
  assign new_n38 = ~new_n36 & ~new_n37;
  assign new_n39 = new_n22 & ~new_n38;
  assign new_n40 = ~new_n22 & new_n38;
  assign new_n41 = ~new_n39 & ~new_n40;
  assign new_n42 = n27 & new_n41;
  assign new_n43 = ~n27 & ~new_n41;
  assign new_n44 = ~new_n42 & ~new_n43;
  assign new_n45 = ~new_n23 & ~new_n26_1;
  assign new_n46_1 = ~new_n25 & ~new_n45;
  assign new_n47 = ~new_n44 & new_n46_1;
  assign new_n48 = new_n44 & ~new_n46_1;
  assign n34 = new_n47 | new_n48;
  assign new_n50 = new_n31 & ~new_n33;
  assign new_n51 = ~new_n34_1 & ~new_n50;
  assign new_n52_1 = ~n32 & n65;
  assign new_n53 = n46 & ~n61;
  assign new_n54_1 = ~new_n52_1 & new_n53;
  assign new_n55 = new_n52_1 & ~new_n53;
  assign new_n56 = ~new_n54_1 & ~new_n55;
  assign new_n57 = ~new_n51 & ~new_n56;
  assign new_n58_1 = new_n51 & new_n56;
  assign new_n59 = ~new_n57 & ~new_n58_1;
  assign new_n60 = ~new_n39 & ~new_n59;
  assign new_n61_1 = new_n39 & new_n59;
  assign new_n62 = ~new_n60 & ~new_n61_1;
  assign new_n63 = n10 & ~n13;
  assign new_n64_1 = n58 & ~n66;
  assign new_n65_1 = new_n63 & ~new_n64_1;
  assign new_n66_1 = ~new_n63 & new_n64_1;
  assign new_n67 = ~new_n65_1 & ~new_n66_1;
  assign new_n68 = ~new_n62 & new_n67;
  assign new_n69 = new_n62 & ~new_n67;
  assign new_n70 = ~new_n68 & ~new_n69;
  assign new_n71 = ~new_n42 & ~new_n46_1;
  assign new_n72 = ~new_n43 & ~new_n71;
  assign new_n73 = n64 & new_n72;
  assign new_n74 = ~n64 & ~new_n72;
  assign new_n75 = ~new_n73 & ~new_n74;
  assign new_n76 = new_n70 & new_n75;
  assign new_n77 = ~new_n70 & ~new_n75;
  assign n52 = ~new_n76 & ~new_n77;
  assign new_n79 = n4 & ~new_n20;
  assign n54 = ~new_n24 & ~new_n79;
endmodule


