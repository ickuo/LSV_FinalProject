// Benchmark "top" written by ABC on Sun Dec 21 18:07:08 2025

module top ( 
    n4, n23, n25, n28, n32, n38, n50, n55, n58, n60, n63, n65,
    n14, n34, n40, n59  );
  input  n4, n23, n25, n28, n32, n38, n50, n55, n58, n60, n63, n65;
  output n14, n34, n40, n59;
  wire new_n17, new_n18, new_n19, new_n20, new_n21, new_n22, new_n23_1,
    new_n24, new_n25_1, new_n26, new_n27, new_n28_1, new_n29, new_n30,
    new_n31, new_n32_1, new_n33, new_n34_1, new_n35, new_n36, new_n37,
    new_n38_1, new_n39, new_n40_1, new_n41, new_n42, new_n43, new_n44,
    new_n46, new_n47, new_n48, new_n50_1, new_n52, new_n53, new_n54,
    new_n55_1, new_n56, new_n57, new_n58_1, new_n59_1, new_n60_1, new_n61,
    new_n62, new_n63_1, new_n64, new_n65_1, new_n66, new_n67, new_n68,
    new_n69, new_n70, new_n71, new_n72, new_n73, new_n74, new_n75, new_n76,
    new_n77, new_n78, new_n79;
  assign new_n17 = n25 & n32;
  assign new_n18 = n38 & n50;
  assign new_n19 = new_n17 & new_n18;
  assign new_n20 = n50 & n60;
  assign new_n21 = n32 & n38;
  assign new_n22 = n25 & n63;
  assign new_n23_1 = ~new_n21 & ~new_n22;
  assign new_n24 = new_n21 & new_n22;
  assign new_n25_1 = ~new_n23_1 & ~new_n24;
  assign new_n26 = new_n20 & ~new_n25_1;
  assign new_n27 = ~new_n20 & new_n25_1;
  assign new_n28_1 = ~new_n26 & ~new_n27;
  assign new_n29 = ~new_n19 & new_n28_1;
  assign new_n30 = new_n19 & ~new_n28_1;
  assign new_n31 = ~new_n29 & ~new_n30;
  assign new_n32_1 = n65 & new_n31;
  assign new_n33 = ~n65 & ~new_n31;
  assign new_n34_1 = ~new_n32_1 & ~new_n33;
  assign new_n35 = ~new_n17 & ~new_n18;
  assign new_n36 = ~new_n19 & ~new_n35;
  assign new_n37 = ~n23 & ~new_n36;
  assign new_n38_1 = n23 & new_n36;
  assign new_n39 = n25 & n50;
  assign new_n40_1 = n4 & new_n39;
  assign new_n41 = ~new_n38_1 & ~new_n40_1;
  assign new_n42 = ~new_n37 & ~new_n41;
  assign new_n43 = ~new_n34_1 & new_n42;
  assign new_n44 = new_n34_1 & ~new_n42;
  assign n14 = new_n43 | new_n44;
  assign new_n46 = ~new_n37 & ~new_n38_1;
  assign new_n47 = ~new_n40_1 & new_n46;
  assign new_n48 = new_n40_1 & ~new_n46;
  assign n34 = new_n47 | new_n48;
  assign new_n50_1 = ~n4 & ~new_n39;
  assign n40 = ~new_n40_1 & ~new_n50_1;
  assign new_n52 = new_n20 & ~new_n23_1;
  assign new_n53 = ~new_n24 & ~new_n52;
  assign new_n54 = ~n28 & ~new_n53;
  assign new_n55_1 = n28 & new_n53;
  assign new_n56 = ~new_n54 & ~new_n55_1;
  assign new_n57 = n50 & n58;
  assign new_n58_1 = n25 & n55;
  assign new_n59_1 = ~new_n57 & ~new_n58_1;
  assign new_n60_1 = new_n57 & new_n58_1;
  assign new_n61 = ~new_n59_1 & ~new_n60_1;
  assign new_n62 = n38 & n63;
  assign new_n63_1 = n32 & n60;
  assign new_n64 = new_n62 & ~new_n63_1;
  assign new_n65_1 = ~new_n62 & new_n63_1;
  assign new_n66 = ~new_n64 & ~new_n65_1;
  assign new_n67 = ~new_n61 & ~new_n66;
  assign new_n68 = new_n61 & new_n66;
  assign new_n69 = ~new_n67 & ~new_n68;
  assign new_n70 = ~new_n56 & new_n69;
  assign new_n71 = new_n56 & ~new_n69;
  assign new_n72 = ~new_n70 & ~new_n71;
  assign new_n73 = new_n30 & ~new_n72;
  assign new_n74 = ~new_n30 & new_n72;
  assign new_n75 = ~new_n73 & ~new_n74;
  assign new_n76 = ~new_n33 & new_n42;
  assign new_n77 = ~new_n32_1 & ~new_n76;
  assign new_n78 = ~new_n75 & ~new_n77;
  assign new_n79 = new_n75 & new_n77;
  assign n59 = new_n78 | new_n79;
endmodule


