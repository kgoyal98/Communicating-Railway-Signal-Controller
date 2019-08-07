----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date:    14:49:43 01/23/2018 
-- Design Name: 
-- Module Name:    encrypter - Behavioral 
-- Project Name: 
-- Target Devices: 
-- Tool versions: 
-- Description: 
--
-- Dependencies: 
--
-- Revision: 
-- Revision 0.01 - File Created
-- Additional Comments: 
--
----------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.std_logic_unsigned.all;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx primitives in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity encrypter is
    Port ( clock : in  STD_LOGIC;
           K : in  STD_LOGIC_VECTOR (31 downto 0);
           P : in  STD_LOGIC_VECTOR (31 downto 0);
           C : out  STD_LOGIC_VECTOR (31 downto 0);
           reset : in  STD_LOGIC;
           enable : in  STD_LOGIC;
		   done: out STD_LOGIC := '0');
end encrypter;
architecture Behavioral of encrypter is
	signal i: INTEGER range 0 to 34 := 0;
	signal T: std_logic_vector(3 downto 0) := "0000";
	signal Cp: std_logic_vector(31 downto 0) := "00000000000000000000000000000000";
begin
	process(clock, reset)
	begin
		if (reset = '1') then
			Cp <= "00000000000000000000000000000000";
			i <= 0;
			T <= "0000";
			done <= '0';
			C <= (others => '0');
		elsif (clock'event and clock = '1') then
			if (enable = '1') then
				if(i = 0) then
					Cp <= P;
					done <= '0';
					T(3) <=  K(31) xor K(27) xor K(23) xor K(19) xor k(15) xor K(11) xor K(7) xor K(3);
					T(2) <=  K(30) xor K(26) xor K(22) xor K(18) xor k(14) xor K(10) xor K(6) xor K(2);
					T(1) <=  K(29) xor K(25) xor K(21) xor K(17) xor k(13) xor K(9) xor K(5) xor K(1);
					T(0) <=  K(28) xor K(24) xor K(20) xor K(16) xor k(12) xor K(8) xor K(4) xor K(0);
					i <= 1;
				elsif (i < 33 and i > 0) then
					if (K(i-1) = '0') then
						i <= i+1;
					else
						Cp <= Cp xor (T & T & T & T & T & T & T & T);
						T <= T+1;
						i <= i+1;
					end if;
				else
					C <= Cp;
					done <= '1';
				end if;
			end if;
		end if;
	end process;

end Behavioral;