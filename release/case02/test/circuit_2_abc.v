module top( n3 , n4 , n6 , n9 , n10 , n13 , n15 , n18 , n30 , 
n31 , n33 , n38 , n40 , n44 , n48 , n49 );
    input n3 , n4 , n9 , n13 , n15 , n30 , n31 , n38 , n40 , 
n44 , n48 , n49 ;
    output n6 , n10 , n18 , n33 ;
    wire n0 , n1 , n2 , n5 , n7 , n8 , n11 , n12 , n14 , 
n16 , n17 , n19 , n20 , n21 , n22 , n23 , n24 , n25 , n26 , 
n27 , n28 , n29 , n32 , n34 , n35 , n36 , n37 , n39 , n41 , 
n42 , n43 , n45 , n46 , n47 ;
    nor ( n41 , n49 , n48 );
    xnor ( n33 , n24 , n27 );
    or ( n43 , n45 , n42 );
    and ( n19 , n22 , n11 );
    and ( n39 , n1 , n21 );
    or ( n22 , n17 , n20 );
    nor ( n2 , n35 , n39 );
    xnor ( n8 , n9 , n40 );
    not ( n17 , n44 );
    and ( n34 , n31 , n46 );
    nor ( n35 , n15 , n38 );
    not ( n45 , n3 );
    xnor ( n18 , n20 , n5 );
    xnor ( n27 , n8 , n47 );
    nor ( n26 , n44 , n34 );
    and ( n0 , n37 , n19 );
    not ( n37 , n30 );
    xnor ( n10 , n23 , n31 );
    xnor ( n12 , n39 , n16 );
    nor ( n14 , n12 , n0 );
    not ( n46 , n23 );
    not ( n20 , n34 );
    not ( n42 , n4 );
    xnor ( n28 , n43 , n7 );
    or ( n11 , n26 , n28 );
    nand ( n1 , n49 , n48 );
    xnor ( n23 , n3 , n4 );
    or ( n29 , n25 , n2 );
    nor ( n32 , n37 , n19 );
    or ( n21 , n43 , n41 );
    xnor ( n16 , n15 , n38 );
    xnor ( n24 , n29 , n13 );
    xnor ( n36 , n12 , n30 );
    xnor ( n6 , n19 , n36 );
    xnor ( n7 , n49 , n48 );
    xnor ( n5 , n28 , n44 );
    or ( n47 , n32 , n14 );
    and ( n25 , n15 , n38 );
endmodule
